/*
 * Minimal WebSocket Client over TLS (OpenSSL)
 *
 * Implements RFC 6455 client-side WebSocket protocol:
 * - wss:// URL parsing
 * - TCP connect + TLS handshake
 * - HTTP Upgrade handshake with Sec-WebSocket-Key
 * - Binary frame send (masked) / receive
 * - Ping/Pong handling
 *
 * Uses OpenSSL BIO/SSL for TLS — no extra dependencies.
 */

#include "ws_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

struct ws_client {
    SSL_CTX *ctx;
    SSL     *ssl;
    int      fd;
    int      connected;
    char     host[256];
    char     path[1024];
    int      port;
};

/* ── URL parsing ──────────────────────────────────────────── */

static int parse_wss_url(const char *url, char *host, size_t host_sz,
                         char *path, size_t path_sz, int *port)
{
    *port = 443;
    const char *p = url;

    /* Skip scheme */
    if (strncmp(p, "wss://", 6) == 0) {
        p += 6;
    } else if (strncmp(p, "ws://", 5) == 0) {
        p += 5;
        *port = 80;
    } else {
        return -1;
    }

    /* Host (may include :port) */
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    size_t host_len;
    if (colon && (!slash || colon < slash)) {
        host_len = (size_t)(colon - p);
        *port = atoi(colon + 1);
    } else if (slash) {
        host_len = (size_t)(slash - p);
    } else {
        host_len = strlen(p);
    }

    if (host_len >= host_sz) host_len = host_sz - 1;
    memcpy(host, p, host_len);
    host[host_len] = '\0';

    /* Path (everything from first /) */
    if (slash) {
        snprintf(path, path_sz, "%s", slash);
    } else {
        snprintf(path, path_sz, "/");
    }

    return 0;
}

/* ── Base64 encoding for Sec-WebSocket-Key ────────────────── */

static int base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_sz)
{
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t pos = 0;

    for (size_t i = 0; i < in_len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < in_len) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < in_len) v |= (uint32_t)in[i + 2];

        if (pos + 4 >= out_sz) return -1;
        out[pos++] = b64[(v >> 18) & 0x3F];
        out[pos++] = b64[(v >> 12) & 0x3F];
        out[pos++] = (i + 1 < in_len) ? b64[(v >> 6) & 0x3F] : '=';
        out[pos++] = (i + 2 < in_len) ? b64[v & 0x3F] : '=';
    }
    out[pos] = '\0';
    return (int)pos;
}

/* ── TCP connect ──────────────────────────────────────────── */

static int tcp_connect(const char *host, int port)
{
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, port_str, &hints, &res);
    if (rc != 0 || !res) return -1;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return -1;
    }

    /* Connect with 10s timeout */
    struct timeval tv = {.tv_sec = 10, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(fd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return fd;
}

/* ── TLS setup ────────────────────────────────────────────── */

static SSL_CTX *create_ssl_ctx(void)
{
    const SSL_METHOD *method = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) return NULL;

    SSL_CTX_set_default_verify_paths(ctx);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    return ctx;
}

/* ── WebSocket Upgrade handshake ──────────────────────────── */

static int ws_handshake(ws_client_t *ws)
{
    /* Generate random 16-byte key */
    uint8_t key_bytes[16];
    RAND_bytes(key_bytes, sizeof(key_bytes));
    char key_b64[32];
    base64_encode(key_bytes, sizeof(key_bytes), key_b64, sizeof(key_b64));

    /* Build HTTP Upgrade request */
    char req[2048];
    int len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        ws->path, ws->host, key_b64);

    if (SSL_write(ws->ssl, req, len) <= 0)
        return -1;

    /* Read response (look for "101") */
    char resp[4096];
    int total = 0;
    while (total < (int)sizeof(resp) - 1) {
        int n = SSL_read(ws->ssl, resp + total, (int)sizeof(resp) - 1 - total);
        if (n <= 0) return -1;
        total += n;
        resp[total] = '\0';
        if (strstr(resp, "\r\n\r\n")) break;
    }

    if (!strstr(resp, "101"))
        return -1;

    return 0;
}

/* ── Frame I/O ────────────────────────────────────────────── */

static int ssl_read_exact(SSL *ssl, uint8_t *buf, size_t len)
{
    size_t got = 0;
    while (got < len) {
        int n = SSL_read(ssl, buf + got, (int)(len - got));
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

/* ── Send helpers ─────────────────────────────────────────── */

static int ws_send_frame(ws_client_t *ws, uint8_t opcode_byte,
                         const uint8_t *data, size_t len)
{
    if (!ws || !ws->connected) return -1;

    uint8_t header[14];
    size_t hlen = 0;

    /* FIN + opcode, MASK bit set (client must mask) */
    header[hlen++] = opcode_byte;

    if (len < 126) {
        header[hlen++] = (uint8_t)(0x80 | len);
    } else if (len <= 0xFFFF) {
        header[hlen++] = 0x80 | 126;
        header[hlen++] = (uint8_t)(len >> 8);
        header[hlen++] = (uint8_t)(len & 0xFF);
    } else {
        header[hlen++] = 0x80 | 127;
        for (int i = 7; i >= 0; i--)
            header[hlen++] = (uint8_t)((len >> (i * 8)) & 0xFF);
    }

    /* Masking key */
    uint8_t mask[4];
    RAND_bytes(mask, 4);
    memcpy(header + hlen, mask, 4);
    hlen += 4;

    /* Send header */
    if (SSL_write(ws->ssl, header, (int)hlen) <= 0) {
        ws->connected = 0;
        return -1;
    }

    /* Send masked payload */
    if (len > 0) {
        uint8_t *masked = malloc(len);
        if (!masked) return -1;
        for (size_t i = 0; i < len; i++)
            masked[i] = data[i] ^ mask[i % 4];
        int rc = SSL_write(ws->ssl, masked, (int)len);
        free(masked);
        if (rc <= 0) {
            ws->connected = 0;
            return -1;
        }
    }

    return 0;
}

int ws_send_text(ws_client_t *ws, const char *text, size_t len)
{
    return ws_send_frame(ws, 0x81, (const uint8_t *)text, len);
}

int ws_send_binary(ws_client_t *ws, const uint8_t *data, size_t len)
{
    return ws_send_frame(ws, 0x82, data, len);
}

int ws_recv(ws_client_t *ws, uint8_t *out_buf, size_t buf_size,
            size_t *out_len, uint8_t *out_opcode, int timeout_ms)
{
    if (!ws || !ws->connected) return -1;
    *out_len = 0;

    /* Poll for data availability */
    if (timeout_ms > 0) {
        /* Check SSL internal buffer first before blocking on poll */
        if (SSL_pending(ws->ssl) == 0) {
            struct pollfd pfd = { .fd = ws->fd, .events = POLLIN };
            int pr = poll(&pfd, 1, timeout_ms);
            if (pr == 0) return 1;  /* timeout */
            if (pr < 0) return -1;
        }
    } else if (timeout_ms == 0) {
        /* Check if SSL has buffered data first */
        if (SSL_pending(ws->ssl) == 0) {
            struct pollfd pfd = { .fd = ws->fd, .events = POLLIN };
            int pr = poll(&pfd, 1, 0);
            if (pr <= 0) return 1;  /* no data */
        }
    }

    /* Read 2-byte header */
    uint8_t hdr[2];
    if (ssl_read_exact(ws->ssl, hdr, 2) < 0) {
        ws->connected = 0;
        return -1;
    }

    uint8_t opcode = hdr[0] & 0x0F;
    int masked = (hdr[1] & 0x80) != 0;
    uint64_t payload_len = hdr[1] & 0x7F;

    if (payload_len == 126) {
        uint8_t ext[2];
        if (ssl_read_exact(ws->ssl, ext, 2) < 0) { ws->connected = 0; return -1; }
        payload_len = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        if (ssl_read_exact(ws->ssl, ext, 8) < 0) { ws->connected = 0; return -1; }
        payload_len = 0;
        for (int i = 0; i < 8; i++)
            payload_len = (payload_len << 8) | ext[i];
    }

    uint8_t mask_key[4] = {0};
    if (masked) {
        if (ssl_read_exact(ws->ssl, mask_key, 4) < 0) { ws->connected = 0; return -1; }
    }

    /* Read payload */
    size_t to_read = (payload_len <= buf_size) ? (size_t)payload_len : buf_size;
    if (to_read > 0) {
        if (ssl_read_exact(ws->ssl, out_buf, to_read) < 0) {
            ws->connected = 0;
            return -1;
        }
    }

    /* Discard excess bytes if payload > buffer */
    if ((size_t)payload_len > buf_size) {
        size_t remaining = (size_t)payload_len - buf_size;
        uint8_t discard[256];
        while (remaining > 0) {
            size_t chunk = remaining > sizeof(discard) ? sizeof(discard) : remaining;
            if (ssl_read_exact(ws->ssl, discard, chunk) < 0) { ws->connected = 0; return -1; }
            remaining -= chunk;
        }
    }

    /* Unmask if needed (server shouldn't mask, but handle it) */
    if (masked) {
        for (size_t i = 0; i < to_read; i++)
            out_buf[i] ^= mask_key[i % 4];
    }

    *out_len = to_read;
    *out_opcode = opcode;

    /* Handle control frames */
    if (opcode == 0x09) {
        /* Ping — send Pong with same payload */
        uint8_t pong_hdr[14];
        size_t phlen = 0;
        pong_hdr[phlen++] = 0x8A; /* FIN + Pong */
        uint8_t pmask[4];
        RAND_bytes(pmask, 4);

        if (to_read < 126) {
            pong_hdr[phlen++] = (uint8_t)(0x80 | to_read);
        } else {
            pong_hdr[phlen++] = 0x80 | 126;
            pong_hdr[phlen++] = (uint8_t)(to_read >> 8);
            pong_hdr[phlen++] = (uint8_t)(to_read & 0xFF);
        }
        memcpy(pong_hdr + phlen, pmask, 4);
        phlen += 4;
        SSL_write(ws->ssl, pong_hdr, (int)phlen);

        if (to_read > 0) {
            uint8_t *pong_data = malloc(to_read);
            if (pong_data) {
                for (size_t i = 0; i < to_read; i++)
                    pong_data[i] = out_buf[i] ^ pmask[i % 4];
                SSL_write(ws->ssl, pong_data, (int)to_read);
                free(pong_data);
            }
        }
    } else if (opcode == 0x08) {
        /* Close frame */
        ws->connected = 0;
    }

    return 0;
}

int ws_is_connected(ws_client_t *ws)
{
    return ws && ws->connected;
}

/* ── Connect ──────────────────────────────────────────────── */

ws_client_t *ws_connect(const char *url)
{
    ws_client_t *ws = calloc(1, sizeof(*ws));
    if (!ws) return NULL;

    if (parse_wss_url(url, ws->host, sizeof(ws->host),
                      ws->path, sizeof(ws->path), &ws->port) < 0) {
        free(ws);
        return NULL;
    }

    /* TCP connect */
    ws->fd = tcp_connect(ws->host, ws->port);
    if (ws->fd < 0) {
        free(ws);
        return NULL;
    }

    /* TLS */
    ws->ctx = create_ssl_ctx();
    if (!ws->ctx) {
        close(ws->fd);
        free(ws);
        return NULL;
    }

    ws->ssl = SSL_new(ws->ctx);
    SSL_set_fd(ws->ssl, ws->fd);
    SSL_set_tlsext_host_name(ws->ssl, ws->host);

    if (SSL_connect(ws->ssl) <= 0) {
        SSL_free(ws->ssl);
        SSL_CTX_free(ws->ctx);
        close(ws->fd);
        free(ws);
        return NULL;
    }

    /* WebSocket Upgrade */
    if (ws_handshake(ws) < 0) {
        SSL_shutdown(ws->ssl);
        SSL_free(ws->ssl);
        SSL_CTX_free(ws->ctx);
        close(ws->fd);
        free(ws);
        return NULL;
    }

    ws->connected = 1;
    return ws;
}

void ws_close(ws_client_t *ws)
{
    if (!ws) return;

    if (ws->ssl) {
        if (ws->connected) {
            /* Send close frame */
            uint8_t close_frame[6];
            close_frame[0] = 0x88; /* FIN + Close */
            close_frame[1] = 0x80; /* Mask, 0 payload */
            RAND_bytes(close_frame + 2, 4);
            SSL_write(ws->ssl, close_frame, 6);
        }
        SSL_shutdown(ws->ssl);
        SSL_free(ws->ssl);
    }
    if (ws->ctx) SSL_CTX_free(ws->ctx);
    if (ws->fd >= 0) close(ws->fd);
    ws->connected = 0;
    free(ws);
}

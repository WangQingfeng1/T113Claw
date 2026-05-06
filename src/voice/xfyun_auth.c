/*
 * iFlytek (讯飞) WebSocket API Authentication
 *
 * Implements HMAC-SHA256 signature per:
 *   https://www.xfyun.cn/doc/asr/voicedictation/API.html#接口鉴权
 *
 * Generates URL: {base_url}?authorization={auth}&date={date}&host={host}
 */

#include "xfyun_auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <openssl/bio.h>

/* ── Base64 encode ────────────────────────────────────────── */

static int base64_encode(const unsigned char *in, int in_len,
                         char *out, int out_size)
{
    BIO *bio = BIO_new(BIO_f_base64());
    BIO *bmem = BIO_new(BIO_s_mem());
    bio = BIO_push(bio, bmem);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, in, in_len);
    BIO_flush(bio);

    BUF_MEM *bptr;
    BIO_get_mem_ptr(bio, &bptr);

    int len = (int)bptr->length;
    if (len >= out_size) len = out_size - 1;
    memcpy(out, bptr->data, len);
    out[len] = '\0';

    BIO_free_all(bio);
    return len;
}

/* ── URL encode ───────────────────────────────────────────── */

static int url_encode(const char *in, char *out, int out_size)
{
    int j = 0;
    for (int i = 0; in[i] && j < out_size - 4; i++) {
        unsigned char c = (unsigned char)in[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[j++] = c;
        } else {
            j += snprintf(out + j, out_size - j, "%%%02X", c);
        }
    }
    out[j] = '\0';
    return j;
}

/* ── Parse host and path from URL ─────────────────────────── */

static int parse_url(const char *url, char *host, int host_sz,
                     char *path, int path_sz)
{
    /* Skip scheme: wss:// or ws:// */
    const char *p = strstr(url, "://");
    if (!p) return -1;
    p += 3;

    const char *slash = strchr(p, '/');
    if (!slash) {
        snprintf(host, host_sz, "%.*s", (int)(strlen(p)), p);
        snprintf(path, path_sz, "/");
    } else {
        int hlen = (int)(slash - p);
        snprintf(host, host_sz, "%.*s", hlen, p);
        snprintf(path, path_sz, "%s", slash);
    }
    return 0;
}

/* ── Build authenticated URL ──────────────────────────────── */

int xfyun_build_auth_url(const char *base_url,
                          const char *api_key,
                          const char *api_secret,
                          char *out_buf, size_t buf_size)
{
    char host[128] = "";
    char path[128] = "";
    if (parse_url(base_url, host, sizeof(host), path, sizeof(path)) < 0)
        return -1;

    /* 1. Generate RFC1123 date (UTC) */
    char date_str[64];
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    strftime(date_str, sizeof(date_str), "%a, %d %b %Y %H:%M:%S GMT", &tm);

    /* 2. Build signature_origin:
     *    host: {host}\ndate: {date}\nGET {path} HTTP/1.1 */
    char sig_origin[512];
    snprintf(sig_origin, sizeof(sig_origin),
             "host: %s\ndate: %s\nGET %s HTTP/1.1", host, date_str, path);

    /* 3. HMAC-SHA256(sig_origin, api_secret) → signature_sha */
    unsigned char hmac_out[32];
    unsigned int hmac_len = 0;
    HMAC(EVP_sha256(),
         api_secret, (int)strlen(api_secret),
         (const unsigned char *)sig_origin, strlen(sig_origin),
         hmac_out, &hmac_len);

    /* 4. signature = base64(signature_sha) */
    char signature[64];
    base64_encode(hmac_out, (int)hmac_len, signature, sizeof(signature));

    /* 5. authorization_origin */
    char auth_origin[512];
    snprintf(auth_origin, sizeof(auth_origin),
             "api_key=\"%s\", algorithm=\"hmac-sha256\", "
             "headers=\"host date request-line\", signature=\"%s\"",
             api_key, signature);

    /* 6. authorization = base64(authorization_origin) */
    char authorization[768];
    base64_encode((const unsigned char *)auth_origin, (int)strlen(auth_origin),
                  authorization, sizeof(authorization));

    /* 7. URL-encode date and authorization */
    char date_enc[256];
    char auth_enc[1024];
    url_encode(date_str, date_enc, sizeof(date_enc));
    url_encode(authorization, auth_enc, sizeof(auth_enc));

    /* 8. Build final URL */
    int written = snprintf(out_buf, buf_size,
                           "%s?authorization=%s&date=%s&host=%s",
                           base_url, auth_enc, date_enc, host);
    if (written < 0 || (size_t)written >= buf_size) return -1;

    return 0;
}

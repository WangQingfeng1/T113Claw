/*
 * iFlytek STT (语音听写) WebSocket Client
 *
 * Streams raw PCM audio to 讯飞语音听写流式版 and returns text.
 *
 * Protocol:
 *  - Connect to wss://iat-api.xfyun.cn/v2/iat with HMAC auth
 *  - Send JSON text frames with base64-encoded PCM audio
 *  - First frame: common + business + data(status=0)
 *  - Middle frames: data(status=1, audio=base64)
 *  - Last frame: data(status=2)
 *  - Receive JSON: data.result.ws[].cw[].w → accumulate text
 *  - data.status == 2 → recognition complete
 */

#include "xfyun_stt.h"
#include "xfyun_auth.h"
#include "ws_client.h"
#include "utils/log.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#define TAG "stt"

#define XFYUN_STT_URL    "wss://iat-api.xfyun.cn/v2/iat"
#define FRAME_SIZE        1280   /* 40ms of 16kHz 16bit mono = 1280 bytes */
#define FRAME_INTERVAL_US 40000  /* 40ms between frames */

static char s_app_id[32];
static char s_api_key[64];
static char s_api_secret[64];

/* ── Base64 encode (reusable) ─────────────────────────────── */

static int b64_encode(const unsigned char *in, int in_len,
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

/* ── Extract text from STT response ──────────────────────── */

static void extract_stt_text(const char *json_str, char *out, size_t out_size,
                              size_t *out_offset, int *done)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return;

    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (code && code->valueint != 0) {
        cJSON *msg = cJSON_GetObjectItem(root, "message");
        LOG_E(TAG, "STT error %d: %s", code->valueint,
              msg ? msg->valuestring : "unknown");
        *done = 1;
        cJSON_Delete(root);
        return;
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) { cJSON_Delete(root); return; }

    cJSON *status = cJSON_GetObjectItem(data, "status");
    if (status && status->valueint == 2)
        *done = 1;

    cJSON *result = cJSON_GetObjectItem(data, "result");
    if (!result) { cJSON_Delete(root); return; }

    cJSON *ws = cJSON_GetObjectItem(result, "ws");
    if (!ws || !cJSON_IsArray(ws)) { cJSON_Delete(root); return; }

    int arr_size = cJSON_GetArraySize(ws);
    for (int i = 0; i < arr_size; i++) {
        cJSON *item = cJSON_GetArrayItem(ws, i);
        cJSON *cw = cJSON_GetObjectItem(item, "cw");
        if (!cw || !cJSON_IsArray(cw)) continue;

        int cw_size = cJSON_GetArraySize(cw);
        for (int j = 0; j < cw_size; j++) {
            cJSON *word = cJSON_GetArrayItem(cw, j);
            cJSON *w = cJSON_GetObjectItem(word, "w");
            if (w && w->valuestring) {
                size_t wlen = strlen(w->valuestring);
                if (*out_offset + wlen < out_size - 1) {
                    memcpy(out + *out_offset, w->valuestring, wlen);
                    *out_offset += wlen;
                    out[*out_offset] = '\0';
                }
            }
        }
    }

    cJSON_Delete(root);
}

/* ── Public API ───────────────────────────────────────────── */

int xfyun_stt_init(const char *app_id, const char *api_key, const char *api_secret)
{
    if (!app_id || !api_key || !api_secret) return -1;
    snprintf(s_app_id, sizeof(s_app_id), "%s", app_id);
    snprintf(s_api_key, sizeof(s_api_key), "%s", api_key);
    snprintf(s_api_secret, sizeof(s_api_secret), "%s", api_secret);
    LOG_I(TAG, "iFlytek STT initialized (appid=%s)", s_app_id);
    return 0;
}

int xfyun_stt_recognize(const uint8_t *pcm, size_t pcm_len,
                         char *out_text, size_t text_size)
{
    if (!pcm || pcm_len == 0 || !out_text || text_size == 0) return -1;
    out_text[0] = '\0';

    /* 1. Build auth URL */
    char auth_url[2048];
    if (xfyun_build_auth_url(XFYUN_STT_URL, s_api_key, s_api_secret,
                              auth_url, sizeof(auth_url)) < 0) {
        LOG_E(TAG, "Failed to build auth URL");
        return -1;
    }

    /* 2. Connect WebSocket */
    ws_client_t *ws = ws_connect(auth_url);
    if (!ws) {
        LOG_E(TAG, "WebSocket connect failed");
        return -1;
    }

    LOG_I(TAG, "STT connected, sending %zu bytes PCM", pcm_len);

    /* 3. Stream PCM in frames */
    size_t offset = 0;
    int frame_num = 0;
    int send_err = 0;

    /* Allocate JSON buffer: base64 of 1280 bytes = ~1708 chars + JSON overhead */
    char *json_buf = malloc(4096);
    char *audio_b64 = malloc(2048);
    if (!json_buf || !audio_b64) {
        free(json_buf); free(audio_b64);
        ws_close(ws);
        return -1;
    }

    while (offset < pcm_len && !send_err) {
        size_t chunk = pcm_len - offset;
        if (chunk > FRAME_SIZE) chunk = FRAME_SIZE;

        int status;
        if (frame_num == 0) status = 0;           /* first frame */
        else if (offset + chunk >= pcm_len) status = 2; /* last */
        else status = 1;                           /* middle */

        /* Base64 encode audio chunk */
        b64_encode(pcm + offset, (int)chunk, audio_b64, 2048);

        if (frame_num == 0) {
            /* First frame: include common + business params */
            snprintf(json_buf, 4096,
                "{"
                "\"common\":{\"app_id\":\"%s\"},"
                "\"business\":{"
                    "\"language\":\"zh_cn\","
                    "\"domain\":\"iat\","
                    "\"accent\":\"mandarin\","
                    "\"vad_eos\":3000,"
                    "\"ptt\":1"
                "},"
                "\"data\":{"
                    "\"status\":%d,"
                    "\"format\":\"audio/L16;rate=16000\","
                    "\"encoding\":\"raw\","
                    "\"audio\":\"%s\""
                "}"
                "}", s_app_id, status, audio_b64);
        } else {
            /* Subsequent frames: data only */
            snprintf(json_buf, 4096,
                "{\"data\":{\"status\":%d,\"audio\":\"%s\"}}",
                status, audio_b64);
        }

        if (ws_send_text(ws, json_buf, strlen(json_buf)) < 0) {
            LOG_E(TAG, "Send frame %d failed", frame_num);
            send_err = 1;
            break;
        }

        offset += chunk;
        frame_num++;

        /* Pace: 40ms between frames (must not flood the server) */
        if (offset < pcm_len)
            usleep(FRAME_INTERVAL_US);
    }

    /* Send end marker if we haven't already */
    if (!send_err && (frame_num == 0 || offset > 0)) {
        /* If the last chunk wasn't status=2, send an empty end frame */
        if (offset < pcm_len || frame_num == 0) {
            const char *end = "{\"data\":{\"status\":2}}";
            ws_send_text(ws, end, strlen(end));
        }
    }

    free(audio_b64);

    /* 4. Receive results until done */
    size_t recv_buf_size = 4096;
    uint8_t *recv_buf = malloc(recv_buf_size + 1); /* +1 for NUL */
    if (!recv_buf) { free(json_buf); ws_close(ws); return -1; }
    size_t text_offset = 0;
    int done = 0;
    int recv_timeout = 0;

    while (!done && recv_timeout < 300) { /* max ~30s */
        size_t rlen = 0;
        uint8_t opcode = 0;
        int rc = ws_recv(ws, recv_buf, recv_buf_size, &rlen, &opcode, 100);
        if (rc < 0) {
            LOG_E(TAG, "Receive error");
            break;
        }
        if (rc == 1) { /* timeout */
            recv_timeout++;
            continue;
        }
        recv_timeout = 0;

        if (opcode == 0x01 && rlen > 0) { /* text frame */
            recv_buf[rlen] = '\0'; /* safe: buf is recv_buf_size+1 */
            extract_stt_text((const char *)recv_buf, out_text, text_size,
                             &text_offset, &done);
        }
    }
    free(recv_buf);

    free(json_buf);
    ws_close(ws);

    if (text_offset > 0) {
        LOG_I(TAG, "STT result: %s", out_text);
        return 0;
    }

    LOG_W(TAG, "STT returned empty result");
    return -1;
}

void xfyun_stt_cleanup(void)
{
    /* nothing to free */
}

/* ── Streaming API ────────────────────────────────────────── */

struct xfyun_stt_stream {
    ws_client_t *ws;
    int frame_num;
    int end_sent;
    char *json_buf;      /* 8192 bytes */
    char *audio_b64;     /* 4096 bytes */
};

xfyun_stt_stream_t *xfyun_stt_stream_open(void)
{
    /* 1. Build auth URL */
    char auth_url[2048];
    if (xfyun_build_auth_url(XFYUN_STT_URL, s_api_key, s_api_secret,
                              auth_url, sizeof(auth_url)) < 0) {
        LOG_E(TAG, "Stream: failed to build auth URL");
        return NULL;
    }

    /* 2. Connect WebSocket */
    ws_client_t *ws = ws_connect(auth_url);
    if (!ws) {
        LOG_E(TAG, "Stream: WebSocket connect failed");
        return NULL;
    }

    /* 3. Allocate stream */
    xfyun_stt_stream_t *s = calloc(1, sizeof(*s));
    if (!s) {
        ws_close(ws);
        return NULL;
    }
    s->ws = ws;
    s->json_buf = malloc(8192);
    s->audio_b64 = malloc(4096);
    if (!s->json_buf || !s->audio_b64) {
        free(s->json_buf);
        free(s->audio_b64);
        free(s);
        ws_close(ws);
        return NULL;
    }

    LOG_I(TAG, "STT stream opened");
    return s;
}

int xfyun_stt_stream_send(xfyun_stt_stream_t *s,
                           const uint8_t *pcm, size_t len,
                           int is_last)
{
    if (!s || !s->ws) return -1;
    if (s->end_sent) return -1;

    int status;
    if (s->frame_num == 0) status = 0;         /* first */
    else if (is_last)      status = 2;         /* last */
    else                   status = 1;         /* middle */

    /* Base64 encode audio chunk */
    if (pcm && len > 0) {
        b64_encode(pcm, (int)len, s->audio_b64, 4096);
    }

    /* Build JSON */
    if (s->frame_num == 0) {
        if (pcm && len > 0) {
            snprintf(s->json_buf, 8192,
                "{"
                "\"common\":{\"app_id\":\"%s\"},"
                "\"business\":{"
                    "\"language\":\"zh_cn\","
                    "\"domain\":\"iat\","
                    "\"accent\":\"mandarin\","
                    "\"vad_eos\":3000,"
                    "\"ptt\":1"
                "},"
                "\"data\":{"
                    "\"status\":%d,"
                    "\"format\":\"audio/L16;rate=16000\","
                    "\"encoding\":\"raw\","
                    "\"audio\":\"%s\""
                "}"
                "}", s_app_id, status, s->audio_b64);
        } else {
            /* First frame with no audio — just send end marker */
            snprintf(s->json_buf, 8192,
                "{"
                "\"common\":{\"app_id\":\"%s\"},"
                "\"business\":{"
                    "\"language\":\"zh_cn\","
                    "\"domain\":\"iat\","
                    "\"accent\":\"mandarin\","
                    "\"vad_eos\":3000,"
                    "\"ptt\":1"
                "},"
                "\"data\":{"
                    "\"status\":2,"
                    "\"format\":\"audio/L16;rate=16000\","
                    "\"encoding\":\"raw\""
                "}"
                "}", s_app_id);
            is_last = 1;
        }
    } else {
        if (pcm && len > 0) {
            snprintf(s->json_buf, 8192,
                "{\"data\":{\"status\":%d,\"audio\":\"%s\"}}",
                status, s->audio_b64);
        } else {
            snprintf(s->json_buf, 8192,
                "{\"data\":{\"status\":2}}");
            is_last = 1;
        }
    }

    if (ws_send_text(s->ws, s->json_buf, strlen(s->json_buf)) < 0) {
        LOG_E(TAG, "Stream: send frame %d failed", s->frame_num);
        return -1;
    }

    s->frame_num++;
    if (is_last) s->end_sent = 1;
    return 0;
}

int xfyun_stt_stream_finish(xfyun_stt_stream_t *s,
                             char *out_text, size_t text_size)
{
    if (!s || !s->ws || !out_text || text_size == 0) return -1;
    out_text[0] = '\0';

    /* Ensure end frame was sent */
    if (!s->end_sent) {
        if (s->frame_num == 0) {
            xfyun_stt_stream_send(s, NULL, 0, 1);
        } else {
            const char *end = "{\"data\":{\"status\":2}}";
            ws_send_text(s->ws, end, strlen(end));
            s->end_sent = 1;
        }
    }

    LOG_I(TAG, "STT stream: waiting for result (%d frames sent)", s->frame_num);

    /* Receive results until done */
    size_t recv_buf_size = 4096;
    uint8_t *recv_buf = malloc(recv_buf_size + 1);
    if (!recv_buf) return -1;

    size_t text_offset = 0;
    int done = 0;
    int recv_timeout = 0;

    while (!done && recv_timeout < 300) { /* max ~30s */
        size_t rlen = 0;
        uint8_t opcode = 0;
        int rc = ws_recv(s->ws, recv_buf, recv_buf_size, &rlen, &opcode, 100);
        if (rc < 0) {
            LOG_E(TAG, "Stream: receive error");
            break;
        }
        if (rc == 1) { /* timeout */
            recv_timeout++;
            continue;
        }
        recv_timeout = 0;

        if (opcode == 0x01 && rlen > 0) {
            recv_buf[rlen] = '\0';
            extract_stt_text((const char *)recv_buf, out_text, text_size,
                             &text_offset, &done);
        }
    }

    free(recv_buf);

    if (text_offset > 0) {
        LOG_I(TAG, "STT stream result: %s", out_text);
        return 0;
    }

    LOG_W(TAG, "STT stream returned empty result");
    return -1;
}

void xfyun_stt_stream_close(xfyun_stt_stream_t *s)
{
    if (!s) return;
    if (s->ws) ws_close(s->ws);
    free(s->json_buf);
    free(s->audio_b64);
    free(s);
}

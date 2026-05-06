/*
 * iFlytek TTS (语音合成) WebSocket Client
 *
 * Sends text to 讯飞在线语音合成, streams back PCM audio via callback.
 *
 * Protocol:
 *  - Connect to wss://tts-api.xfyun.cn/v2/tts with HMAC auth
 *  - Send single JSON text frame: common + business + data(status=2, text=base64)
 *  - Receive multiple JSON frames with data.audio = base64-encoded PCM
 *  - data.status == 2 → synthesis complete
 */

#include "xfyun_tts.h"
#include "xfyun_auth.h"
#include "ws_client.h"
#include "utils/log.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#define TAG "tts"

#define XFYUN_TTS_URL "wss://tts-api.xfyun.cn/v2/tts"

static char s_app_id[32];
static char s_api_key[64];
static char s_api_secret[64];

/* ── Abort flag (set externally to interrupt recv loop) ────── */
static const volatile int *s_abort_flag = NULL;

/* ── Base64 encode/decode ─────────────────────────────────── */

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

static int b64_decode(const char *in, int in_len,
                      unsigned char *out, int out_size)
{
    BIO *bio = BIO_new_mem_buf(in, in_len);
    BIO *b64 = BIO_new(BIO_f_base64());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

    int decoded_len = BIO_read(bio, out, out_size);
    BIO_free_all(bio);
    return decoded_len > 0 ? decoded_len : 0;
}

/* ── Public API ───────────────────────────────────────────── */

int xfyun_tts_init(const char *app_id, const char *api_key, const char *api_secret)
{
    if (!app_id || !api_key || !api_secret) return -1;
    snprintf(s_app_id, sizeof(s_app_id), "%s", app_id);
    snprintf(s_api_key, sizeof(s_api_key), "%s", api_key);
    snprintf(s_api_secret, sizeof(s_api_secret), "%s", api_secret);
    LOG_I(TAG, "iFlytek TTS initialized (appid=%s)", s_app_id);
    return 0;
}

int xfyun_tts_synthesize(const char *text, const char *vcn,
                          tts_audio_cb_t cb, void *user)
{
    if (!text || !text[0] || !cb) return -1;
    if (!vcn || !vcn[0]) vcn = "xiaoyan";

    /* 1. Build auth URL */
    char auth_url[2048];
    if (xfyun_build_auth_url(XFYUN_TTS_URL, s_api_key, s_api_secret,
                              auth_url, sizeof(auth_url)) < 0) {
        LOG_E(TAG, "Failed to build auth URL");
        return -1;
    }

    /* 2. Connect */
    ws_client_t *ws = ws_connect(auth_url);
    if (!ws) {
        LOG_E(TAG, "WebSocket connect failed");
        return -1;
    }

    /* 3. Base64 encode the text */
    size_t text_len = strlen(text);
    size_t b64_buf_size = (text_len * 4 / 3) + 16;
    char *text_b64 = malloc(b64_buf_size);
    if (!text_b64) { ws_close(ws); return -1; }
    b64_encode((const unsigned char *)text, (int)text_len,
               text_b64, (int)b64_buf_size);

    /* 4. Build request JSON */
    size_t json_size = b64_buf_size + 512;
    char *json_buf = malloc(json_size);
    if (!json_buf) { free(text_b64); ws_close(ws); return -1; }

    snprintf(json_buf, json_size,
        "{"
        "\"common\":{\"app_id\":\"%s\"},"
        "\"business\":{"
            "\"aue\":\"raw\","
            "\"auf\":\"audio/L16;rate=16000\","
            "\"vcn\":\"%s\","
            "\"speed\":50,"
            "\"volume\":80,"
            "\"pitch\":50,"
            "\"tte\":\"UTF8\""
        "},"
        "\"data\":{"
            "\"status\":2,"
            "\"text\":\"%s\""
        "}"
        "}", s_app_id, vcn, text_b64);

    free(text_b64);

    LOG_I(TAG, "TTS request: vcn=%s text_len=%zu", vcn, text_len);

    /* 5. Send request */
    if (ws_send_text(ws, json_buf, strlen(json_buf)) < 0) {
        LOG_E(TAG, "Send TTS request failed");
        free(json_buf);
        ws_close(ws);
        return -1;
    }

    /* 6. Receive audio frames */
    /* Reuse json_buf as receive buffer (big enough) */
    size_t recv_size = 64 * 1024;
    uint8_t *recv_buf = malloc(recv_size + 1); /* +1 for NUL */
    unsigned char *pcm_buf = malloc(recv_size);
    int done = 0;
    int recv_timeout = 0;
    int total_pcm = 0;

    if (!recv_buf || !pcm_buf) {
        free(recv_buf); free(pcm_buf); free(json_buf);
        ws_close(ws);
        return -1;
    }

    while (!done && recv_timeout < 300) { /* max ~30s */
        /* Check abort flag (set by voice manager on interrupt) */
        if (s_abort_flag && *s_abort_flag) {
            LOG_I(TAG, "TTS aborted by caller");
            break;
        }
        size_t rlen = 0;
        uint8_t opcode = 0;
        int rc = ws_recv(ws, recv_buf, recv_size, &rlen, &opcode, 100);
        if (rc < 0) {
            LOG_E(TAG, "Receive error");
            break;
        }
        if (rc == 1) { /* timeout */
            recv_timeout++;
            continue;
        }
        recv_timeout = 0;

        if (opcode == 0x01 && rlen > 0) { /* text frame = JSON */
            recv_buf[rlen] = '\0'; /* safe: buf is recv_size+1 */

            cJSON *root = cJSON_Parse((char *)recv_buf);
            if (!root) continue;

            cJSON *code = cJSON_GetObjectItem(root, "code");
            if (code && code->valueint != 0) {
                cJSON *msg = cJSON_GetObjectItem(root, "message");
                LOG_E(TAG, "TTS error %d: %s", code->valueint,
                      msg ? msg->valuestring : "unknown");
                cJSON_Delete(root);
                done = 1;
                break;
            }

            cJSON *data = cJSON_GetObjectItem(root, "data");
            if (data) {
                cJSON *status = cJSON_GetObjectItem(data, "status");
                if (status && status->valueint == 2)
                    done = 1;

                cJSON *audio = cJSON_GetObjectItem(data, "audio");
                if (audio && audio->valuestring && audio->valuestring[0]) {
                    int pcm_len = b64_decode(audio->valuestring,
                                              (int)strlen(audio->valuestring),
                                              pcm_buf, 64 * 1024);
                    if (pcm_len > 0) {
                        cb(pcm_buf, (size_t)pcm_len, user);
                        total_pcm += pcm_len;
                    }
                }
            }

            cJSON_Delete(root);
        }
    }

    LOG_I(TAG, "TTS complete: %d bytes PCM delivered", total_pcm);

    free(recv_buf);
    free(pcm_buf);
    free(json_buf);
    ws_close(ws);

    return total_pcm > 0 ? 0 : -1;
}

void xfyun_tts_cleanup(void)
{
    s_abort_flag = NULL;
}

void xfyun_tts_set_abort(const volatile int *flag)
{
    s_abort_flag = flag;
}

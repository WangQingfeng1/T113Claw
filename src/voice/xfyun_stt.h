#pragma once
/*
 * iFlytek STT (语音听写) WebSocket Client
 *
 * Streams PCM audio to 讯飞语音听写流式接口, returns recognized text.
 * Protocol: wss://iat-api.xfyun.cn/v2/iat
 *
 * Two modes:
 *   1. Batch:     xfyun_stt_recognize()     — record all then send
 *   2. Streaming:  xfyun_stt_stream_*()     — send while recording (low latency)
 */

#include <stddef.h>
#include <stdint.h>

/* Initialize STT module with iFlytek credentials */
int xfyun_stt_init(const char *app_id, const char *api_key, const char *api_secret);

/*
 * Recognize speech from a PCM buffer (batch mode).
 *
 * pcm:       PCM audio data (S16_LE, 16kHz, mono)
 * pcm_len:   length in bytes
 * out_text:  buffer to receive recognized text (UTF-8)
 * text_size: size of out_text
 *
 * Returns 0 on success, -1 on error.
 * Blocking call: connects, streams audio, waits for result, disconnects.
 */
int xfyun_stt_recognize(const uint8_t *pcm, size_t pcm_len,
                         char *out_text, size_t text_size);

/* ── Streaming API (low-latency: send audio while recording) ── */

typedef struct xfyun_stt_stream xfyun_stt_stream_t;

/*
 * Open a streaming STT session (connects WebSocket).
 * Returns stream handle on success, NULL on failure.
 */
xfyun_stt_stream_t *xfyun_stt_stream_open(void);

/*
 * Send a PCM chunk to the STT stream.
 * pcm/len:   PCM data (S16_LE, 16kHz, mono). NULL+0 for empty end frame.
 * is_last:   1 = this is the final chunk (sends status=2)
 * Returns 0 on success, -1 on error.
 */
int xfyun_stt_stream_send(xfyun_stt_stream_t *s,
                           const uint8_t *pcm, size_t len,
                           int is_last);

/*
 * Finish the stream: ensure end frame is sent, read all results.
 * Blocking call. Fills out_text with accumulated recognized text.
 * Returns 0 on success (text found), -1 on error/empty.
 */
int xfyun_stt_stream_finish(xfyun_stt_stream_t *s,
                             char *out_text, size_t text_size);

/*
 * Close and free the stream. Safe to call on NULL.
 */
void xfyun_stt_stream_close(xfyun_stt_stream_t *s);

/* Cleanup */
void xfyun_stt_cleanup(void);

#pragma once
/*
 * iFlytek TTS (语音合成) WebSocket Client
 *
 * Sends text, receives PCM audio via callback.
 * Protocol: wss://tts-api.xfyun.cn/v2/tts
 */

#include <stddef.h>
#include <stdint.h>

/* Callback for received PCM audio data */
typedef void (*tts_audio_cb_t)(const uint8_t *pcm, size_t len, void *user);

/* Initialize TTS module with iFlytek credentials */
int xfyun_tts_init(const char *app_id, const char *api_key, const char *api_secret);

/*
 * Synthesize text to speech, streaming PCM via callback.
 *
 * text:     UTF-8 text to synthesize
 * vcn:      voice name (e.g., "xiaoyan")
 * cb:       callback for PCM chunks (S16_LE, 16kHz, mono)
 * user:     user context for callback
 *
 * Returns 0 on success, -1 on error.
 * Blocking call: connects, sends text, streams audio, disconnects.
 */
int xfyun_tts_synthesize(const char *text, const char *vcn,
                          tts_audio_cb_t cb, void *user);

/* Cleanup */
void xfyun_tts_cleanup(void);

/*
 * Set an external abort flag for TTS synthesis.
 * When *flag becomes non-zero, the recv loop in xfyun_tts_synthesize exits early.
 * Call with NULL to clear.
 */
void xfyun_tts_set_abort(const volatile int *flag);

/*
 * T113Claw Voice Manager — v2 (optimized)
 *
 * Improvements over v1:
 *   - Wake word feedback: plays cached "您好，我在听您说" prompt after wake
 *   - Streaming STT: sends PCM to iFlytek while recording (low latency)
 *   - Push-to-talk: button release = immediate stop recording
 *   - Clean recording: stops capture after wake, restarts for STT
 *   - VAD grace period: ignores first 500ms to avoid false triggers
 *   - Reduced VAD silence: 1.0s (was 1.5s)
 *
 * State machine:
 *   WAKE_LISTENING → [wake/button] → LISTENING (streaming STT) →
 *   RECOGNIZING → THINKING → SPEAKING → WAKE_LISTENING
 *
 * Threading model:
 *   - Audio service streams captured PCM via callback
 *   - gpio_button runs a poll thread, calls button_cb
 *   - voice_manager thread processes state transitions + sends STT frames
 */

#include "voice_manager.h"
#include "vad.h"
#include "wake_detector.h"
#include "xfyun_stt.h"
#include "xfyun_tts.h"
#include "xfyun_auth.h"
#include "gpio_button.h"
#include "services/audio_service.h"
#include "audio_ipc.h"
#include "bus/message_bus.h"
#include "ui/ui_manager.h"
#include "utils/log.h"
#include "t113claw_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#define TAG "voice"

/* ── Voice states ─────────────────────────────────────────── */

typedef enum {
    VOICE_WAKE_LISTENING,   /* continuous capture → wake word detection */
    VOICE_LISTENING,        /* recording speech (VAD active) + streaming STT */
    VOICE_RECOGNIZING,      /* waiting for STT result */
    VOICE_THINKING,         /* waiting for agent */
    VOICE_SPEAKING,         /* TTS playback */
} voice_state_t;

static const char *state_names[] = {
    "WAKE_LISTENING", "LISTENING", "RECOGNIZING", "THINKING", "SPEAKING"
};

static const char *state_labels[] = {
    "待唤醒", "聆听中", "识别中", "思考中", "回答中"
};

/* ── PCM recording buffer ─────────────────────────────────── */

#define MAX_RECORD_SECONDS  30
#define PCM_BUF_SIZE        (AUDIO_SAMPLE_RATE * 2 * AUDIO_CAPTURE_CHANNELS * MAX_RECORD_SECONDS)

/* ── Configuration ────────────────────────────────────────── */

#define VAD_FRAME_SAMPLES   320     /* 20ms frame at 16kHz */
#define VAD_SILENCE_STOP_MS 1000    /* 1.0s silence → stop (reduced from 1.5s) */
#define VAD_MIN_SPEECH_MS   300     /* minimum speech to accept */
#define VAD_GRACE_MS        500     /* ignore VAD for first 500ms after record start */
#define LISTEN_TIMEOUT_MS   15000   /* 15s max listening */
#define STT_FRAME_SIZE      1280    /* 40ms of 16kHz 16-bit mono = 1280 bytes */

/* ── Wake prompt ──────────────────────────────────────────── */

#ifndef T113CLAW_WAKE_PROMPT_TEXT
#define T113CLAW_WAKE_PROMPT_TEXT "您好，我在听您说"
#endif
#define WAKE_PROMPT_MAX_SIZE (256 * 1024) /* 256KB max (~4s stereo) */

static uint8_t *s_wake_prompt = NULL;
static size_t   s_wake_prompt_len = 0;

/* ── State ────────────────────────────────────────────────── */

static volatile voice_state_t s_state = VOICE_WAKE_LISTENING;
static pthread_t       s_thread;
static volatile int    s_running;

/* Wake/button event signals */
static pthread_mutex_t s_evt_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_evt_cond   = PTHREAD_COND_INITIALIZER;
static volatile int    s_btn_pressed;
static volatile int    s_btn_wake_event;   /* set on press, cleared by consumer */
static volatile int    s_wake_detected;

/* Interrupt flag (set by button during SPEAKING to abort TTS) */
static volatile int    s_interrupt;

/* Agent response delivery */
static pthread_mutex_t s_resp_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_resp_cond  = PTHREAD_COND_INITIALIZER;
static char           *s_response_text;

/* Recording buffer */
static uint8_t        *s_pcm_buf;
static size_t          s_pcm_len;
static pthread_mutex_t s_pcm_mutex  = PTHREAD_MUTEX_INITIALIZER;

/* Recording start timestamp (for grace period) */
static struct timespec s_record_start_time;

/* ── Helpers ──────────────────────────────────────────────── */

static void set_state(voice_state_t st)
{
    LOG_I(TAG, "State: %s → %s", state_names[s_state], state_names[st]);
    s_state = st;
    ui_manager_notify_voice_state(state_labels[st]);
}

static long elapsed_ms_since(const struct timespec *start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start->tv_sec) * 1000 +
           (now.tv_nsec - start->tv_nsec) / 1000000;
}

/* ── Wake prompt caching ─────────────────────────────────── */
/*
 * Pre-generate the wake feedback audio ("您好，我在听您说") at init time.
 * Cached as stereo PCM for instant playback on each wake event.
 * Falls back to a generated tone if TTS fails.
 */

struct prompt_cache {
    uint8_t *buf;
    size_t   len;
    size_t   cap;
};

static void prompt_tts_cb(const uint8_t *mono_pcm, size_t len, void *user)
{
    struct prompt_cache *cache = (struct prompt_cache *)user;
    size_t stereo_len = len * 2;
    if (cache->len + stereo_len > cache->cap) return;

    const int16_t *src = (const int16_t *)mono_pcm;
    int16_t *dst = (int16_t *)(cache->buf + cache->len);
    size_t n_samples = len / 2;
    for (size_t i = 0; i < n_samples; i++) {
        dst[i * 2]     = src[i]; /* L */
        dst[i * 2 + 1] = src[i]; /* R */
    }
    cache->len += stereo_len;
}

static void generate_fallback_tone(uint8_t *buf, size_t *out_len)
{
    /* 200ms, 800Hz sine tone with fade — minimal but audible feedback */
    int sample_rate = AUDIO_SAMPLE_RATE;
    int duration_ms = 200;
    float freq = 800.0f;
    int n_samples = sample_rate * duration_ms / 1000;
    int fade_samples = sample_rate * 30 / 1000; /* 30ms fade */
    int16_t *dst = (int16_t *)buf;

    for (int i = 0; i < n_samples; i++) {
        float gain = 1.0f;
        if (i < fade_samples) gain = (float)i / fade_samples;
        else if (i > n_samples - fade_samples)
            gain = (float)(n_samples - i) / fade_samples;
        int16_t sample = (int16_t)(12000.0f * sinf(2.0f * 3.14159265f * freq * i / sample_rate) * gain);
        dst[i * 2]     = sample; /* L */
        dst[i * 2 + 1] = sample; /* R */
    }
    *out_len = (size_t)(n_samples * 2 * sizeof(int16_t));
}

static void cache_wake_prompt(void)
{
    struct prompt_cache cache = {
        .buf = malloc(WAKE_PROMPT_MAX_SIZE),
        .len = 0,
        .cap = WAKE_PROMPT_MAX_SIZE,
    };
    if (!cache.buf) return;

    if (xfyun_tts_synthesize(T113CLAW_WAKE_PROMPT_TEXT, T113CLAW_TTS_VOICE,
                              prompt_tts_cb, &cache) == 0 && cache.len > 0) {
        s_wake_prompt = cache.buf;
        s_wake_prompt_len = cache.len;
        LOG_I(TAG, "Wake prompt cached: %zu bytes (%.1fs)",
              cache.len, (float)cache.len / (AUDIO_SAMPLE_RATE * AUDIO_PLAY_CHANNELS * 2));
    } else {
        LOG_W(TAG, "TTS failed for wake prompt, using fallback tone");
        generate_fallback_tone(cache.buf, &cache.len);
        s_wake_prompt = cache.buf;
        s_wake_prompt_len = cache.len;
    }
}

static void play_wake_prompt(void)
{
    if (!s_wake_prompt || s_wake_prompt_len == 0) return;

    audio_service_play_start();
    audio_service_play_pcm(s_wake_prompt, s_wake_prompt_len);

    /* Graceful drain: audio process waits for ring buffer + ALSA to finish,
     * so the prompt plays completely without needing a fragile usleep. */
    audio_service_play_stop();
}

/* ── Audio capture callback ───────────────────────────────── */
/*
 * Called from the audio service receiver thread with captured PCM.
 * Format: S16_LE, 16kHz, mono.
 *
 * In WAKE_LISTENING: feed to wake word detector.
 * In LISTENING: feed to VAD + accumulate in recording buffer.
 */
static void audio_data_cb(const uint8_t *pcm, size_t len, void *user)
{
    (void)user;
    voice_state_t st = s_state;
    int n_samples = (int)(len / 2);  /* S16_LE → 2 bytes per sample */

    if (st == VOICE_WAKE_LISTENING) {
        /* Feed to wake word detector */
        wake_detector_feed_pcm((const int16_t *)pcm, n_samples);

        /* Check if keyword triggered */
        const char *kw = wake_detector_get_keyword();
        if (kw) {
            pthread_mutex_lock(&s_evt_mutex);
            s_wake_detected = 1;
            pthread_cond_signal(&s_evt_cond);
            pthread_mutex_unlock(&s_evt_mutex);
        }
    } else if (st == VOICE_LISTENING) {
        /* Process VAD frame by frame (only after grace period) */
        long elapsed = elapsed_ms_since(&s_record_start_time);
        if (elapsed >= VAD_GRACE_MS) {
            int16_t *samples = (int16_t *)pcm;
            int offset = 0;
            while (offset + VAD_FRAME_SAMPLES <= n_samples) {
                vad_process(samples + offset, VAD_FRAME_SAMPLES);
                offset += VAD_FRAME_SAMPLES;
            }
        }

        /* Accumulate PCM for STT streaming */
        pthread_mutex_lock(&s_pcm_mutex);
        if (s_pcm_len + len <= PCM_BUF_SIZE) {
            memcpy(s_pcm_buf + s_pcm_len, pcm, len);
            s_pcm_len += len;
        }
        pthread_mutex_unlock(&s_pcm_mutex);

        /* Signal voice thread if VAD says stop (after grace period) */
        if (elapsed >= VAD_GRACE_MS &&
            vad_speech_duration_ms() >= VAD_MIN_SPEECH_MS &&
            vad_silence_duration_ms() >= VAD_SILENCE_STOP_MS) {
            pthread_mutex_lock(&s_evt_mutex);
            pthread_cond_signal(&s_evt_cond);
            pthread_mutex_unlock(&s_evt_mutex);
        }
    }
}

/* ── Button callback ──────────────────────────────────────── */
/*
 * Called from GPIO poll thread on press/release.
 * During SPEAKING: press interrupts TTS playback.
 * Otherwise: press triggers wake; release stops recording in PTT mode.
 */
static void button_cb(int pressed, void *user)
{
    (void)user;
    pthread_mutex_lock(&s_evt_mutex);
    s_btn_pressed = pressed;
    if (pressed) {
        if (s_state == VOICE_SPEAKING) {
            s_interrupt = 1;         /* interrupt TTS playback */
            s_btn_wake_event = 1;    /* also trigger new conversation after interrupt */
        } else {
            s_btn_wake_event = 1;    /* persistent flag, cleared by consumer */
        }
    }
    pthread_cond_signal(&s_evt_cond);
    pthread_mutex_unlock(&s_evt_mutex);
}

/* Wait for wake word or button press */
static int wait_wake_event(void)
{
    pthread_mutex_lock(&s_evt_mutex);
    while (s_running && !s_wake_detected && !s_btn_wake_event)
        pthread_cond_wait(&s_evt_cond, &s_evt_mutex);
    int woke_by_btn = s_btn_wake_event;
    s_btn_wake_event = 0;
    s_wake_detected = 0;
    pthread_mutex_unlock(&s_evt_mutex);
    return woke_by_btn; /* 1=button, 0=wake word */
}

/*
 * Check if listening should stop.
 * For button mode: stop when button is released.
 * For wake word mode: stop when VAD detects enough speech + silence.
 * Returns 1 if should stop, 0 if should continue.
 */
static int should_stop_listening(int woke_by_btn, long elapsed_ms)
{
    /* Timeout check */
    if (elapsed_ms >= LISTEN_TIMEOUT_MS) {
        LOG_W(TAG, "Listening timeout (%ds), speech %dms",
              LISTEN_TIMEOUT_MS / 1000, vad_speech_duration_ms());
        return 1;
    }

    if (woke_by_btn) {
        /* PTT mode: stop on button release */
        if (!s_btn_pressed) {
            LOG_I(TAG, "Button released → stop recording (speech %dms)",
                  vad_speech_duration_ms());
            return 1;
        }
    } else {
        /* Wake word mode: stop on VAD silence (after grace period) */
        if (elapsed_ms >= VAD_GRACE_MS &&
            vad_speech_duration_ms() >= VAD_MIN_SPEECH_MS &&
            vad_silence_duration_ms() >= VAD_SILENCE_STOP_MS) {
            LOG_I(TAG, "VAD: speech %dms + silence %dms → stop",
                  vad_speech_duration_ms(), vad_silence_duration_ms());
            return 1;
        }
    }

    return 0;
}

/* ── TTS audio callback (mono→stereo + playback) ─────────── */
#define TTS_STEREO_BUF_SIZE (128 * 1024)
static uint8_t s_stereo_buf[TTS_STEREO_BUF_SIZE];

static void tts_audio_cb(const uint8_t *pcm, size_t len, void *user)
{
    (void)user;
    if (len == 0 || s_interrupt) return;  /* skip if interrupted */

    size_t stereo_len = len * 2;
    if (stereo_len > TTS_STEREO_BUF_SIZE) {
        LOG_W(TAG, "TTS chunk too large (%zu), truncating", len);
        len = TTS_STEREO_BUF_SIZE / 2;
        stereo_len = TTS_STEREO_BUF_SIZE;
    }

    const int16_t *src = (const int16_t *)pcm;
    int16_t *dst = (int16_t *)s_stereo_buf;
    size_t n_samples = len / 2;

    for (size_t i = 0; i < n_samples; i++) {
        dst[i * 2]     = src[i];
        dst[i * 2 + 1] = src[i];
    }

    audio_service_play_pcm(s_stereo_buf, stereo_len);
}

/* ── Voice manager thread ─────────────────────────────────── */

static void *voice_thread(void *arg)
{
    (void)arg;
    char stt_text[2048];

    while (s_running) {
        /* ════════════════════════════════════════════════════
         * WAKE_LISTENING: wait for wake word or button
         * ════════════════════════════════════════════════════ */
        set_state(VOICE_WAKE_LISTENING);

        /* Start continuous capture for wake word detection */
        if (audio_service_record_start(audio_data_cb, NULL) < 0) {
            LOG_E(TAG, "Failed to start capture for wake listening");
            break;
        }

        wake_detector_reset();
        int woke_by_btn = wait_wake_event();
        if (!s_running) {
            audio_service_record_stop();
            break;
        }

        LOG_I(TAG, "Woke up by %s", woke_by_btn ? "button" : "wake word");

        /* ── Stop recording to avoid contaminating STT ───── */
        audio_service_record_stop();

        /* ── Play wake feedback prompt ────────────────────── */
        play_wake_prompt();

        /* ════════════════════════════════════════════════════
         * LISTENING: record with VAD + stream to STT in parallel
         * ════════════════════════════════════════════════════ */
        set_state(VOICE_LISTENING);
        vad_reset();

        pthread_mutex_lock(&s_pcm_mutex);
        s_pcm_len = 0;
        pthread_mutex_unlock(&s_pcm_mutex);

        /* Open STT stream (WebSocket connection) */
        xfyun_stt_stream_t *stt_stream = xfyun_stt_stream_open();
        if (!stt_stream) {
            LOG_E(TAG, "STT stream open failed");
            continue;
        }

        /* Start fresh recording */
        clock_gettime(CLOCK_MONOTONIC, &s_record_start_time);
        if (audio_service_record_start(audio_data_cb, NULL) < 0) {
            LOG_E(TAG, "Failed to start capture for recording");
            xfyun_stt_stream_close(stt_stream);
            continue;
        }

        /* ── Listening loop: send to STT while recording ──── */
        size_t stt_sent = 0;
        struct timespec last_send_time = {0, 0};
        int stop_requested = 0;

        while (s_running && !stop_requested) {
            long elapsed = elapsed_ms_since(&s_record_start_time);

            /* Check stop conditions */
            if (should_stop_listening(woke_by_btn, elapsed)) {
                stop_requested = 1;
                break;
            }

            /* Send available PCM to STT in FRAME_SIZE chunks */
            pthread_mutex_lock(&s_pcm_mutex);
            size_t avail = s_pcm_len - stt_sent;
            pthread_mutex_unlock(&s_pcm_mutex);

            if (avail >= STT_FRAME_SIZE) {
                /* Rate limit: at least 20ms between sends */
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                long send_elapsed = (now.tv_sec - last_send_time.tv_sec) * 1000 +
                                   (now.tv_nsec - last_send_time.tv_nsec) / 1000000;
                if (last_send_time.tv_sec != 0 && send_elapsed < 20) {
                    usleep((unsigned int)(20 - send_elapsed) * 1000);
                }

                if (xfyun_stt_stream_send(stt_stream,
                                           s_pcm_buf + stt_sent,
                                           STT_FRAME_SIZE, 0) < 0) {
                    LOG_E(TAG, "STT stream send failed");
                    stop_requested = 1;
                    break;
                }
                stt_sent += STT_FRAME_SIZE;
                clock_gettime(CLOCK_MONOTONIC, &last_send_time);
            } else {
                /* No data ready yet, short sleep */
                usleep(10000); /* 10ms */
            }
        }

        /* Stop capture */
        audio_service_record_stop();

        pthread_mutex_lock(&s_pcm_mutex);
        size_t pcm_len = s_pcm_len;
        pthread_mutex_unlock(&s_pcm_mutex);

        LOG_I(TAG, "Recorded %zu bytes (%.1f s), sent %zu to STT",
              pcm_len, (float)pcm_len / (AUDIO_SAMPLE_RATE * 2), stt_sent);

        if (pcm_len < 3200) { /* less than 100ms */
            LOG_W(TAG, "Recording too short, ignoring");
            xfyun_stt_stream_close(stt_stream);
            continue;
        }

        /* Send remaining PCM in chunks + end frame */
        while (stt_sent < pcm_len) {
            size_t remaining = pcm_len - stt_sent;
            size_t chunk = (remaining > STT_FRAME_SIZE) ? STT_FRAME_SIZE : remaining;
            int last = (stt_sent + chunk >= pcm_len) ? 1 : 0;
            xfyun_stt_stream_send(stt_stream, s_pcm_buf + stt_sent,
                                   chunk, last);
            stt_sent += chunk;
        }
        if (!pcm_len || stt_sent == 0) {
            /* No data was recorded, send end marker */
            xfyun_stt_stream_send(stt_stream, NULL, 0, 1);
        }

        /* ════════════════════════════════════════════════════
         * RECOGNIZING: wait for STT result
         * ════════════════════════════════════════════════════ */
        set_state(VOICE_RECOGNIZING);
        stt_text[0] = '\0';

        int stt_rc = xfyun_stt_stream_finish(stt_stream, stt_text, sizeof(stt_text));
        xfyun_stt_stream_close(stt_stream);

        if (stt_rc < 0 || stt_text[0] == '\0') {
            LOG_W(TAG, "STT returned empty or failed, ignoring");
            continue;
        }

        LOG_I(TAG, "STT: \"%s\"", stt_text);

        /* ════════════════════════════════════════════════════
         * THINKING: push to agent, wait for response
         * ════════════════════════════════════════════════════ */
        set_state(VOICE_THINKING);

        /* Clear any stale response from a previous conversation cycle */
        pthread_mutex_lock(&s_resp_mutex);
        if (s_response_text) {
            LOG_W(TAG, "Discarding stale response: %.40s...", s_response_text);
            free(s_response_text);
            s_response_text = NULL;
        }
        pthread_mutex_unlock(&s_resp_mutex);

        mc_msg_t msg = {0};
        snprintf(msg.channel, sizeof(msg.channel), "%s", T113CLAW_CHAN_VOICE);
        snprintf(msg.chat_id, sizeof(msg.chat_id), "voice/local");
        msg.content = strdup(stt_text);

        if (message_bus_push_inbound(&msg) != 0) {
            LOG_E(TAG, "Failed to push to bus");
            free(msg.content);
            continue;
        }

        /* Wait for agent response with 30s timeout */
        pthread_mutex_lock(&s_resp_mutex);
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 30;

        while (s_running && !s_response_text) {
            int rc = pthread_cond_timedwait(&s_resp_cond, &s_resp_mutex, &ts);
            if (rc == ETIMEDOUT) {
                LOG_W(TAG, "Agent response timeout (30s)");
                break;
            }
        }

        char *resp = s_response_text;
        s_response_text = NULL;
        pthread_mutex_unlock(&s_resp_mutex);

        if (!s_running || !resp) {
            free(resp);
            continue;
        }

        LOG_I(TAG, "Agent: %.80s%s", resp, strlen(resp) > 80 ? "..." : "");

        /* ════════════════════════════════════════════════════
         * SPEAKING: TTS playback (interruptible by button)
         * ════════════════════════════════════════════════════ */
        set_state(VOICE_SPEAKING);

        s_interrupt = 0;
        xfyun_tts_set_abort(&s_interrupt);

        audio_service_play_start();

        if (xfyun_tts_synthesize(resp, T113CLAW_TTS_VOICE,
                                  tts_audio_cb, NULL) < 0) {
            LOG_E(TAG, "TTS failed");
        }

        xfyun_tts_set_abort(NULL);

        if (s_interrupt) {
            LOG_I(TAG, "Speech interrupted by button");
            audio_service_play_flush();  /* immediate stop, drop buffered audio */
        } else {
            audio_service_play_stop();   /* graceful drain: play all remaining audio */
        }
        free(resp);
    }

    return NULL;
}

/* ── Public API ───────────────────────────────────────────── */

int voice_manager_init(void)
{
    /* Init STT/TTS with credentials */
    if (xfyun_stt_init(T113CLAW_SECRET_XFYUN_APPID,
                        T113CLAW_SECRET_XFYUN_APIKEY,
                        T113CLAW_SECRET_XFYUN_APISECRET) < 0)
        return -1;

    if (xfyun_tts_init(T113CLAW_SECRET_XFYUN_APPID,
                        T113CLAW_SECRET_XFYUN_APIKEY,
                        T113CLAW_SECRET_XFYUN_APISECRET) < 0)
        return -1;

    /* Init VAD (20ms frames at 16kHz) */
    if (vad_init(AUDIO_SAMPLE_RATE, VAD_FRAME_SAMPLES) < 0) {
        LOG_E(TAG, "Failed to init VAD");
        return -1;
    }

    /* Init wake word detector */
    if (wake_detector_init(T113CLAW_KWS_MODEL_DIR, T113CLAW_KWS_KEYWORDS_FILE) < 0) {
        LOG_E(TAG, "Failed to init wake detector");
        return -1;
    }

    /* Allocate PCM buffer */
    s_pcm_buf = malloc(PCM_BUF_SIZE);
    if (!s_pcm_buf) {
        LOG_E(TAG, "Failed to allocate PCM buffer (%d bytes)", PCM_BUF_SIZE);
        return -1;
    }

    /* Pre-cache wake prompt (TTS "您好，我在听您说" → stereo PCM) */
    LOG_I(TAG, "Caching wake prompt...");
    cache_wake_prompt();

    LOG_I(TAG, "Voice manager initialized (wake word: %s)", T113CLAW_WAKE_WORD);
    return 0;
}

int voice_manager_start(void)
{
    s_running = 1;
    s_btn_pressed = 0;
    s_btn_wake_event = 0;
    s_wake_detected = 0;
    s_interrupt = 0;
    s_response_text = NULL;

    /* Start button listener */
    if (gpio_button_start(T113CLAW_BUTTON_GPIO, button_cb, NULL) < 0) {
        LOG_E(TAG, "Failed to start button on GPIO %d", T113CLAW_BUTTON_GPIO);
        return -1;
    }

    /* Start voice thread */
    if (pthread_create(&s_thread, NULL, voice_thread, NULL) != 0) {
        LOG_E(TAG, "Failed to start voice thread");
        gpio_button_stop();
        return -1;
    }

    LOG_I(TAG, "Voice manager started (button GPIO %d, wake word enabled)",
          T113CLAW_BUTTON_GPIO);
    return 0;
}

void voice_manager_stop(void)
{
    if (!s_running) return;

    LOG_I(TAG, "Stopping voice manager...");
    s_running = 0;

    /* Wake any waits */
    pthread_mutex_lock(&s_evt_mutex);
    pthread_cond_signal(&s_evt_cond);
    pthread_mutex_unlock(&s_evt_mutex);

    pthread_mutex_lock(&s_resp_mutex);
    pthread_cond_signal(&s_resp_cond);
    pthread_mutex_unlock(&s_resp_mutex);

    gpio_button_stop();
    pthread_join(s_thread, NULL);

    /* Free any pending response */
    pthread_mutex_lock(&s_resp_mutex);
    free(s_response_text);
    s_response_text = NULL;
    pthread_mutex_unlock(&s_resp_mutex);

    free(s_pcm_buf);
    s_pcm_buf = NULL;

    free(s_wake_prompt);
    s_wake_prompt = NULL;
    s_wake_prompt_len = 0;

    wake_detector_cleanup();
    vad_cleanup();
    xfyun_stt_cleanup();
    xfyun_tts_cleanup();

    LOG_I(TAG, "Voice manager stopped");
}

void voice_manager_on_response(const char *text)
{
    if (!text) return;

    pthread_mutex_lock(&s_resp_mutex);
    free(s_response_text);
    s_response_text = strdup(text);
    pthread_cond_signal(&s_resp_cond);
    pthread_mutex_unlock(&s_resp_mutex);
}

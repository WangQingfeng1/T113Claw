/*
 * T113Claw Audio — ALSA Playback
 *
 * Plays audio through the T113-S3 internal codec (audiocodec hw:0,0).
 * DAC → HPOUT → LM4871 amplifier → Speaker.
 * 16kHz stereo S16_LE.
 *
 * Reference: app_sdk_xiaozhi/app_sound/aplay.cpp
 */

#include "audio_playback.h"
#include "audio_codec.h"
#include "audio_ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <alsa/asoundlib.h>

#define TAG "playback"
#define LOG_I(tag, fmt, ...) fprintf(stderr, "[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_E(tag, fmt, ...) fprintf(stderr, "[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_D(tag, fmt, ...) fprintf(stderr, "[D][%s] " fmt "\n", tag, ##__VA_ARGS__)

static pthread_t       s_thread;
static volatile int    s_running;
static volatile int    s_active;
static volatile int    s_draining;  /* 1 = drain mode: exit when ring buffer empty */
static audio_play_cb_t s_callback;
static void           *s_user_data;

static void *playback_thread(void *arg)
{
    (void)arg;
    snd_pcm_t *pcm = NULL;
    uint8_t *buffer = NULL;
    int rc;
    int amp_enabled = 0;

    const char *device = AUDIO_PCM_DEVICE;
    unsigned int sample_rate = AUDIO_SAMPLE_RATE;
    unsigned int channels = AUDIO_PLAY_CHANNELS;

    /* Open PCM for playback */
    rc = snd_pcm_open(&pcm, device, SND_PCM_STREAM_PLAYBACK, 0);
    if (rc < 0) {
        LOG_E(TAG, "Failed to open playback device '%s': %s", device, snd_strerror(rc));
        return NULL;
    }

    /* Configure hardware parameters */
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(pcm, hw_params);

    rc  = snd_pcm_hw_params_set_access(pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    rc |= snd_pcm_hw_params_set_format(pcm, hw_params, SND_PCM_FORMAT_S16_LE);
    rc |= snd_pcm_hw_params_set_channels(pcm, hw_params, channels);
    rc |= snd_pcm_hw_params_set_rate_near(pcm, hw_params, &sample_rate, 0);
    if (rc < 0) {
        LOG_E(TAG, "Failed to set playback HW params: %s", snd_strerror(rc));
        snd_pcm_close(pcm);
        return NULL;
    }

    /* Period: 256 frames (~16ms @ 16kHz), buffer: 4x period */
    snd_pcm_uframes_t period_frames = 256;
    int dir = 0;
    snd_pcm_hw_params_set_period_size_near(pcm, hw_params, &period_frames, &dir);
    snd_pcm_uframes_t buf_frames = period_frames * 4;
    snd_pcm_hw_params_set_buffer_size_near(pcm, hw_params, &buf_frames);

    rc = snd_pcm_hw_params(pcm, hw_params);
    if (rc < 0) {
        LOG_E(TAG, "Failed to apply playback HW params: %s", snd_strerror(rc));
        snd_pcm_close(pcm);
        return NULL;
    }

    /* Read back actual values */
    snd_pcm_hw_params_get_period_size(hw_params, &period_frames, &dir);
    snd_pcm_hw_params_get_rate(hw_params, &sample_rate, 0);
    snd_pcm_hw_params_get_channels(hw_params, &channels);

    size_t frame_bytes = channels * (AUDIO_FORMAT_BITS / 8);
    size_t chunk_bytes = period_frames * frame_bytes;

    LOG_I(TAG, "Playback opened: %uHz %uch, period=%lu frames (%.1fms)",
           sample_rate, channels,
           (unsigned long)period_frames,
           (double)period_frames / sample_rate * 1000.0);

    buffer = malloc(chunk_bytes);
    if (!buffer) {
        LOG_E(TAG, "Failed to allocate playback buffer (%zu bytes)", chunk_bytes);
        snd_pcm_close(pcm);
        return NULL;
    }

    s_active = 1;
    LOG_I(TAG, "Playback started");

    while (s_running) {
        if (!s_callback) {
            /* No data source, sleep briefly */
            usleep(10000);
            continue;
        }

        int got = s_callback(buffer, chunk_bytes, s_user_data);
        if (got <= 0) {
            if (s_draining) {
                /* Drain mode: ring buffer empty, finish up */
                break;
            }
            /* No data yet — do NOT disable amp (data may still be in ALSA buffer
             * or more data may arrive soon from IPC) */
            usleep(5000);  /* 5ms idle wait */
            continue;
        }

        /* Enable amp on first data */
        if (!amp_enabled) {
            audio_amp_enable(1);
            amp_enabled = 1;
        }

        snd_pcm_sframes_t written = snd_pcm_writei(pcm, buffer, got / frame_bytes);
        if (written < 0) {
            LOG_E(TAG, "Playback write error: %s", snd_strerror((int)written));
            snd_pcm_prepare(pcm);
        }
    }

    /* Cleanup: drain ALSA FIRST (play remaining queued audio), THEN disable amp */
    if (s_draining && amp_enabled) {
        snd_pcm_drain(pcm);  /* blocks until all queued audio is played */
    }
    if (amp_enabled) {
        audio_amp_enable(0);
    }
    s_active = 0;
    free(buffer);
    snd_pcm_close(pcm);
    LOG_I(TAG, "Playback stopped (drain=%d)", s_draining);
    return NULL;
}

int audio_playback_init(void)
{
    s_running = 0;
    s_active = 0;
    s_draining = 0;
    s_callback = NULL;
    s_user_data = NULL;
    LOG_I(TAG, "Playback module initialized");
    return 0;
}

int audio_playback_start(audio_play_cb_t cb, void *user)
{
    if (s_running) {
        LOG_E(TAG, "Playback already running");
        return -1;
    }

    s_callback = cb;
    s_user_data = user;
    s_running = 1;
    s_draining = 0;

    if (pthread_create(&s_thread, NULL, playback_thread, NULL) != 0) {
        LOG_E(TAG, "Failed to create playback thread");
        s_running = 0;
        return -1;
    }

    pthread_setname_np(s_thread, "mc_playback");
    return 0;
}

void audio_playback_stop(void)
{
    if (!s_running) return;
    s_running = 0;
    pthread_join(s_thread, NULL);
    s_callback = NULL;
    s_user_data = NULL;
}

void audio_playback_drain(void)
{
    if (!s_running) return;
    s_draining = 1;           /* signal thread to exit when ring buffer empty */
    pthread_join(s_thread, NULL);  /* wait for drain + ALSA drain + amp off */
    s_draining = 0;
    s_running = 0;
    s_callback = NULL;
    s_user_data = NULL;
}

int audio_playback_is_active(void)
{
    return s_active;
}

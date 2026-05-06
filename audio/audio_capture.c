/*
 * T113Claw Audio — ALSA Capture
 *
 * Records from the T113-S3 internal codec (audiocodec hw:0,0).
 * MIC3 differential input → ADC3 → 16kHz mono S16_LE.
 *
 * Reference: app_sdk_xiaozhi/app_sound/record.cpp
 */

#include "audio_capture.h"
#include "audio_ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <alsa/asoundlib.h>

#define TAG "capture"
#define LOG_I(tag, fmt, ...) fprintf(stderr, "[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_E(tag, fmt, ...) fprintf(stderr, "[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_D(tag, fmt, ...) fprintf(stderr, "[D][%s] " fmt "\n", tag, ##__VA_ARGS__)

static pthread_t       s_thread;
static volatile int    s_running;
static volatile int    s_active;
static audio_capture_cb_t s_callback;
static void           *s_user_data;

static void *capture_thread(void *arg)
{
    (void)arg;
    snd_pcm_t *pcm = NULL;
    uint8_t *buffer = NULL;
    int rc;

    const char *device = AUDIO_PCM_DEVICE;
    unsigned int sample_rate = AUDIO_SAMPLE_RATE;
    unsigned int channels = AUDIO_CAPTURE_CHANNELS;

    /* Open PCM for capture */
    rc = snd_pcm_open(&pcm, device, SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0) {
        LOG_E(TAG, "Failed to open capture device '%s': %s", device, snd_strerror(rc));
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
        LOG_E(TAG, "Failed to set capture HW params: %s", snd_strerror(rc));
        snd_pcm_close(pcm);
        return NULL;
    }

    /* Period: 512 frames (~32ms @ 16kHz), buffer: 4x period */
    snd_pcm_uframes_t period_frames = 512;
    int dir = 0;
    snd_pcm_hw_params_set_period_size_near(pcm, hw_params, &period_frames, &dir);
    snd_pcm_uframes_t buf_frames = period_frames * 4;
    snd_pcm_hw_params_set_buffer_size_near(pcm, hw_params, &buf_frames);

    rc = snd_pcm_hw_params(pcm, hw_params);
    if (rc < 0) {
        LOG_E(TAG, "Failed to apply capture HW params: %s", snd_strerror(rc));
        snd_pcm_close(pcm);
        return NULL;
    }

    /* Read back actual period size */
    snd_pcm_hw_params_get_period_size(hw_params, &period_frames, &dir);
    snd_pcm_hw_params_get_rate(hw_params, &sample_rate, 0);
    snd_pcm_hw_params_get_channels(hw_params, &channels);

    size_t frame_bytes = channels * (AUDIO_FORMAT_BITS / 8);
    size_t chunk_bytes = period_frames * frame_bytes;

    LOG_I(TAG, "Capture opened: %uHz %uch, period=%lu frames (%.1fms)",
           sample_rate, channels,
           (unsigned long)period_frames,
           (double)period_frames / sample_rate * 1000.0);

    buffer = malloc(chunk_bytes);
    if (!buffer) {
        LOG_E(TAG, "Failed to allocate capture buffer (%zu bytes)", chunk_bytes);
        snd_pcm_close(pcm);
        return NULL;
    }

    s_active = 1;
    LOG_I(TAG, "Capture started");

    while (s_running) {
        snd_pcm_sframes_t frames = snd_pcm_readi(pcm, buffer, period_frames);
        if (frames == -EPIPE) {
            LOG_E(TAG, "Capture overrun, recovering");
            snd_pcm_prepare(pcm);
            continue;
        } else if (frames < 0) {
            LOG_E(TAG, "Capture read error: %s", snd_strerror((int)frames));
            rc = snd_pcm_recover(pcm, (int)frames, 0);
            if (rc < 0) {
                LOG_E(TAG, "Capture recovery failed: %s", snd_strerror(rc));
                break;
            }
            continue;
        }

        if (s_callback && frames > 0) {
            s_callback(buffer, frames * frame_bytes, s_user_data);
        }
    }

    s_active = 0;
    free(buffer);
    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
    LOG_I(TAG, "Capture stopped");
    return NULL;
}

int audio_capture_init(void)
{
    s_running = 0;
    s_active = 0;
    s_callback = NULL;
    s_user_data = NULL;
    LOG_I(TAG, "Capture module initialized");
    return 0;
}

int audio_capture_start(audio_capture_cb_t cb, void *user)
{
    if (s_running) {
        LOG_E(TAG, "Capture already running");
        return -1;
    }

    s_callback = cb;
    s_user_data = user;
    s_running = 1;

    if (pthread_create(&s_thread, NULL, capture_thread, NULL) != 0) {
        LOG_E(TAG, "Failed to create capture thread");
        s_running = 0;
        return -1;
    }

    pthread_setname_np(s_thread, "mc_capture");
    return 0;
}

void audio_capture_stop(void)
{
    if (!s_running) return;
    s_running = 0;
    pthread_join(s_thread, NULL);
    s_callback = NULL;
    s_user_data = NULL;
}

int audio_capture_is_active(void)
{
    return s_active;
}

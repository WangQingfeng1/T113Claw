#pragma once
/*
 * T113Claw Audio — ALSA Playback
 *
 * Opens ALSA PCM device for playback (HP output, 16kHz, stereo, S16_LE).
 * Runs in a dedicated thread; pulls audio data via callback.
 */

#include <stddef.h>
#include <stdint.h>

/*
 * Callback to fetch playback data.
 * buffer: destination for PCM data
 * size:   requested size in bytes
 * user:   user pointer
 * Returns: actual bytes filled (0 = no data available)
 */
typedef int (*audio_play_cb_t)(uint8_t *buffer, size_t size, void *user);

/* Initialize playback (does not start) */
int audio_playback_init(void);

/* Start the playback thread. Calls cb to pull audio data. */
int audio_playback_start(audio_play_cb_t cb, void *user);

/* Stop playback and join thread */
void audio_playback_stop(void);

/* Graceful drain: wait for ring buffer empty + ALSA drain, then stop */
void audio_playback_drain(void);

/* Check if currently playing */
int audio_playback_is_active(void);

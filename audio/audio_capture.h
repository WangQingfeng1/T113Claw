#pragma once
/*
 * T113Claw Audio — ALSA Capture
 *
 * Opens ALSA PCM device for recording (MIC3, 16kHz, mono, S16_LE).
 * Runs in a dedicated thread; delivers audio via callback.
 */

#include <stddef.h>
#include <stdint.h>

/*
 * Callback invoked for each captured audio chunk.
 * buffer: PCM data (S16_LE, mono, 16kHz)
 * size:   buffer size in bytes
 * user:   user-provided pointer
 */
typedef void (*audio_capture_cb_t)(const uint8_t *buffer, size_t size, void *user);

/* Initialize capture (does not start recording) */
int audio_capture_init(void);

/* Start the capture thread. Calls cb for each audio chunk. */
int audio_capture_start(audio_capture_cb_t cb, void *user);

/* Stop capture and join thread */
void audio_capture_stop(void);

/* Check if currently capturing */
int audio_capture_is_active(void);

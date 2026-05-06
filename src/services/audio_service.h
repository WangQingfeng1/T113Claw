#pragma once
/*
 * T113Claw Audio Service — IPC Client
 *
 * Manages the t113claw_audio child process and communicates via Unix domain
 * socket.  Provides API for the agent and channels to trigger recording
 * and playback without touching ALSA directly.
 *
 * Lifecycle:
 *   audio_service_init()   — locate t113claw_audio binary
 *   audio_service_start()  — fork/exec audio process, connect IPC
 *   audio_service_stop()   — send shutdown, kill child
 *
 * Audio flow:
 *   Record:  audio_service_record_start() → captured PCM via callback
 *   Play:    audio_service_play_pcm()     → send PCM to audio process
 */

#include <stddef.h>
#include <stdint.h>

/* Callback for received audio data (captured PCM) */
typedef void (*audio_data_cb_t)(const uint8_t *pcm, size_t len, void *user);

/* Initialize audio service (find audio process binary) */
int audio_service_init(void);

/* Start the audio process and establish IPC connection */
int audio_service_start(void);

/* Stop audio process and cleanup */
void audio_service_stop(void);

/* ── Recording control ────────────────────────────────────── */

/* Start recording. Captured audio arrives via the registered callback. */
int audio_service_record_start(audio_data_cb_t cb, void *user);

/* Stop recording */
int audio_service_record_stop(void);

/* ── Playback control ─────────────────────────────────────── */

/* Begin playback session (prepares audio process) */
int audio_service_play_start(void);

/* Send a chunk of PCM data for playback (S16_LE, 16kHz, stereo) */
int audio_service_play_pcm(const uint8_t *pcm, size_t len);

/* End playback session (graceful drain: waits for buffered audio to finish) */
int audio_service_play_stop(void);

/* Immediate stop: discard buffered audio and stop playback (for interrupt) */
int audio_service_play_flush(void);

/* ── Volume control ───────────────────────────────────────── */

/* Set playback volume (0-100) */
int audio_service_set_volume(int volume_pct);

/* ── Status ───────────────────────────────────────────────── */

/* Check if audio process is connected and running */
int audio_service_is_running(void);

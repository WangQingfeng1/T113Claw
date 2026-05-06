#pragma once
/*
 * T113Claw VAD — Speex-based Voice Activity Detection
 *
 * Wraps Speex preprocessor to detect speech/silence transitions.
 * Used to auto-stop recording after the user finishes speaking.
 *
 * Usage:
 *   vad_init(16000, 320);      // 16kHz, 20ms frames (320 samples)
 *   for each frame:
 *     int is_speech = vad_process(pcm_s16, 320);
 *     if (vad_silence_duration_ms() > 1500)  // 1.5s silence → stop
 *       break;
 *   vad_reset();                // reset for next utterance
 *   vad_cleanup();
 */

#include <stdint.h>

/* Initialize VAD. frame_size = samples per frame (e.g. 320 for 20ms @ 16kHz). */
int  vad_init(int sample_rate, int frame_size);

/* Process one frame. Returns 1=speech, 0=silence. Modifies pcm in place (denoised). */
int  vad_process(int16_t *pcm, int n_samples);

/* Get duration of continuous silence in milliseconds since last speech. */
int  vad_silence_duration_ms(void);

/* Get duration of continuous speech in milliseconds since last silence. */
int  vad_speech_duration_ms(void);

/* Reset counters (call before each new utterance). */
void vad_reset(void);

/* Cleanup resources. */
void vad_cleanup(void);

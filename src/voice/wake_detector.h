#pragma once
/*
 * T113Claw Wake Word Detector — sherpa-onnx Keyword Spotter
 *
 * Listens continuously for the keyword "你好小爪" using a local
 * on-device neural network (sherpa-onnx zipformer KWS model).
 *
 * Usage:
 *   wake_detector_init("/path/to/model/dir", "keywords_t113claw.txt");
 *   // Feed audio continuously:
 *   wake_detector_feed_pcm(pcm_s16, n_samples);
 *   // Check:
 *   const char *kw = wake_detector_get_keyword();
 *   if (kw) { // woken up! }
 *   wake_detector_reset();
 *   wake_detector_cleanup();
 */

#include <stdint.h>

/*
 * Initialize the wake word detector.
 *   model_dir:    directory containing encoder/decoder/joiner .onnx and tokens.txt
 *   keywords_file: path to keywords file (e.g. "keywords_t113claw.txt")
 * Returns 0 on success, -1 on failure.
 */
int  wake_detector_init(const char *model_dir, const char *keywords_file);

/*
 * Feed PCM audio samples (S16_LE, 16kHz, mono).
 * Internally converts to float and runs decoding.
 *   pcm:       pointer to int16_t samples
 *   n_samples: number of samples
 */
void wake_detector_feed_pcm(const int16_t *pcm, int n_samples);

/*
 * Check if a keyword was detected.
 * Returns the keyword string (e.g. "你好小爪") or NULL if nothing detected.
 * The string is valid until the next call to reset or feed.
 */
const char *wake_detector_get_keyword(void);

/*
 * Reset the stream after a keyword is detected.
 * Must be called after handling a wake word event.
 */
void wake_detector_reset(void);

/* Cleanup all resources. */
void wake_detector_cleanup(void);

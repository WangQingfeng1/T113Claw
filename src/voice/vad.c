/*
 * T113Claw VAD — Speex-based Voice Activity Detection
 *
 * Uses libspeexdsp's preprocessor which provides:
 *   - Noise suppression (denoise)
 *   - Voice activity detection (VAD)
 *
 * speex_preprocess_run() returns 1 for speech, 0 for silence.
 * We track consecutive silence frames to determine when the user stops talking.
 */

#include "vad.h"
#include <speex/speex_preprocess.h>
#include <string.h>

static SpeexPreprocessState *s_pp;
static int s_sample_rate;
static int s_frame_size;

/* Tracking counters */
static int s_silence_frames;       /* consecutive silence frames */
static int s_speech_frames;        /* consecutive speech frames */
static int s_total_speech_frames;  /* cumulative speech frames (not reset on silence) */

int vad_init(int sample_rate, int frame_size)
{
    s_sample_rate = sample_rate;
    s_frame_size = frame_size;
    s_silence_frames = 0;
    s_speech_frames = 0;
    s_total_speech_frames = 0;

    s_pp = speex_preprocess_state_init(frame_size, sample_rate);
    if (!s_pp)
        return -1;

    /* Enable VAD */
    int val = 1;
    speex_preprocess_ctl(s_pp, SPEEX_PREPROCESS_SET_VAD, &val);

    /* Enable denoise */
    val = 1;
    speex_preprocess_ctl(s_pp, SPEEX_PREPROCESS_SET_DENOISE, &val);

    /* Noise suppression: -15 dB */
    val = -15;
    speex_preprocess_ctl(s_pp, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &val);

    /* VAD thresholds (percent probability) */
    val = 80;  /* require 80% probability to go from silence → speech */
    speex_preprocess_ctl(s_pp, SPEEX_PREPROCESS_SET_PROB_START, &val);

    val = 65;  /* require 65% probability to stay in speech */
    speex_preprocess_ctl(s_pp, SPEEX_PREPROCESS_SET_PROB_CONTINUE, &val);

    return 0;
}

int vad_process(int16_t *pcm, int n_samples)
{
    if (!s_pp) return 0;
    (void)n_samples; /* should match s_frame_size */

    int is_speech = speex_preprocess_run(s_pp, pcm);

    if (is_speech) {
        s_speech_frames++;
        s_total_speech_frames++;
        s_silence_frames = 0;
    } else {
        s_silence_frames++;
        s_speech_frames = 0;
    }

    return is_speech;
}

int vad_silence_duration_ms(void)
{
    if (s_sample_rate == 0 || s_frame_size == 0) return 0;
    return (int)((long long)s_silence_frames * s_frame_size * 1000 / s_sample_rate);
}

int vad_speech_duration_ms(void)
{
    if (s_sample_rate == 0 || s_frame_size == 0) return 0;
    return (int)((long long)s_total_speech_frames * s_frame_size * 1000 / s_sample_rate);
}

void vad_reset(void)
{
    s_silence_frames = 0;
    s_speech_frames = 0;
    s_total_speech_frames = 0;
}

void vad_cleanup(void)
{
    if (s_pp) {
        speex_preprocess_state_destroy(s_pp);
        s_pp = NULL;
    }
}

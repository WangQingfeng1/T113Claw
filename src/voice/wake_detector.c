/*
 * T113Claw Wake Word Detector — sherpa-onnx Keyword Spotter
 *
 * Uses sherpa-onnx C API to run a streaming keyword spotting model.
 * Model: sherpa-onnx-kws-zipformer-wenetspeech-3.3M (Chinese, int8)
 *
 * Audio flow:
 *   int16_t PCM → convert to float32 [-1,1] → AcceptWaveform → Decode → check result
 */

#include "wake_detector.h"
#include "utils/log.h"

#include <sherpa-onnx/c-api/c-api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "wake"

static const SherpaOnnxKeywordSpotter *s_kws;
static const SherpaOnnxOnlineStream   *s_stream;
static char s_detected_keyword[128];

/* Temporary buffer for int16→float conversion */
#define CONVERT_BUF_SIZE  4096  /* up to 4096 samples per call */
static float s_float_buf[CONVERT_BUF_SIZE];

int wake_detector_init(const char *model_dir, const char *keywords_file)
{
    SherpaOnnxKeywordSpotterConfig config;
    memset(&config, 0, sizeof(config));

    /* Build model paths */
    char encoder[512], decoder[512], joiner[512], tokens[512];

    snprintf(encoder, sizeof(encoder), "%s/encoder-epoch-12-avg-2-chunk-16-left-64.int8.onnx", model_dir);
    snprintf(decoder, sizeof(decoder), "%s/decoder-epoch-12-avg-2-chunk-16-left-64.onnx", model_dir);
    snprintf(joiner, sizeof(joiner),   "%s/joiner-epoch-12-avg-2-chunk-16-left-64.int8.onnx", model_dir);
    snprintf(tokens, sizeof(tokens),   "%s/tokens.txt", model_dir);

    config.model_config.transducer.encoder = encoder;
    config.model_config.transducer.decoder = decoder;
    config.model_config.transducer.joiner  = joiner;
    config.model_config.tokens = tokens;
    config.model_config.num_threads = 2;
    config.model_config.provider = "cpu";
    config.model_config.debug = 0;

    config.feat_config.sample_rate = 16000;
    config.feat_config.feature_dim = 80;

    config.max_active_paths = 4;
    config.num_trailing_blanks = 1;
    config.keywords_score = 3.0f;
    config.keywords_threshold = 0.25f;
    config.keywords_file = keywords_file;

    s_kws = SherpaOnnxCreateKeywordSpotter(&config);
    if (!s_kws) {
        LOG_E(TAG, "Failed to create keyword spotter");
        return -1;
    }

    s_stream = SherpaOnnxCreateKeywordStream(s_kws);
    if (!s_stream) {
        LOG_E(TAG, "Failed to create keyword stream");
        SherpaOnnxDestroyKeywordSpotter(s_kws);
        s_kws = NULL;
        return -1;
    }

    s_detected_keyword[0] = '\0';
    LOG_I(TAG, "Wake word detector initialized (model: %s)", model_dir);
    return 0;
}

void wake_detector_feed_pcm(const int16_t *pcm, int n_samples)
{
    if (!s_kws || !s_stream) return;

    /* Convert S16 to float32 [-1.0, 1.0] in chunks */
    int offset = 0;
    while (offset < n_samples) {
        int chunk = n_samples - offset;
        if (chunk > CONVERT_BUF_SIZE)
            chunk = CONVERT_BUF_SIZE;

        for (int i = 0; i < chunk; i++)
            s_float_buf[i] = pcm[offset + i] / 32768.0f;

        SherpaOnnxOnlineStreamAcceptWaveform(s_stream, 16000, s_float_buf, chunk);
        offset += chunk;
    }

    /* Decode all available frames */
    while (SherpaOnnxIsKeywordStreamReady(s_kws, s_stream))
        SherpaOnnxDecodeKeywordStream(s_kws, s_stream);
}

const char *wake_detector_get_keyword(void)
{
    if (!s_kws || !s_stream) return NULL;

    const SherpaOnnxKeywordResult *r = SherpaOnnxGetKeywordResult(s_kws, s_stream);
    if (r && r->keyword && r->keyword[0] != '\0') {
        snprintf(s_detected_keyword, sizeof(s_detected_keyword), "%s", r->keyword);
        LOG_I(TAG, "Wake word detected: %s", s_detected_keyword);
        SherpaOnnxDestroyKeywordResult(r);
        return s_detected_keyword;
    }

    if (r) SherpaOnnxDestroyKeywordResult(r);
    return NULL;
}

void wake_detector_reset(void)
{
    if (s_kws && s_stream) {
        SherpaOnnxResetKeywordStream(s_kws, s_stream);
    }
    s_detected_keyword[0] = '\0';
}

void wake_detector_cleanup(void)
{
    if (s_stream) {
        SherpaOnnxDestroyOnlineStream(s_stream);
        s_stream = NULL;
    }
    if (s_kws) {
        SherpaOnnxDestroyKeywordSpotter(s_kws);
        s_kws = NULL;
    }
    s_detected_keyword[0] = '\0';
    LOG_I(TAG, "Wake word detector cleaned up");
}

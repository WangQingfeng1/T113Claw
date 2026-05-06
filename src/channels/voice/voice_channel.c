/*
 * Voice Channel — wraps voice_manager as a unified mc_channel_t
 *
 * Note: On SIMULATOR_LINUX, init/start/stop are no-ops.
 * Voice channel is only active on T113 hardware.
 */

#include "voice_channel.h"
#include "voice/voice_manager.h"
#include "bus/message_bus.h"
#include "t113claw_config.h"
#include "utils/log.h"
#include "utils/utils.h"

#define TAG "voice_ch"

static int voice_ch_init(void)
{
#ifndef SIMULATOR_LINUX
    return voice_manager_init();
#else
    LOG_I(TAG, "Voice channel disabled (simulator)");
    return MC_OK;
#endif
}

static int voice_ch_start(void)
{
#ifndef SIMULATOR_LINUX
    int rc = voice_manager_start();
    if (rc < 0) {
        LOG_W(TAG, "Voice manager start failed (GPIO unavailable?)");
        return MC_OK; /* Non-fatal */
    }
    return MC_OK;
#else
    return MC_OK;
#endif
}

static void voice_ch_stop(void)
{
#ifndef SIMULATOR_LINUX
    voice_manager_stop();
#endif
}

static int voice_ch_send(const char *chat_id, const char *text)
{
    (void)chat_id;
#ifndef SIMULATOR_LINUX
    voice_manager_on_response(text);
#else
    LOG_D(TAG, "Voice send (sim): %.80s", text);
#endif
    return MC_OK;
}

/* ── Channel descriptor ───────────────────────────────────── */

static const mc_channel_t s_voice_descriptor = {
    .name  = T113CLAW_CHAN_VOICE,
    .init  = voice_ch_init,
    .start = voice_ch_start,
    .stop  = voice_ch_stop,
    .send  = voice_ch_send,
};

const mc_channel_t *voice_channel_descriptor(void)
{
    return &s_voice_descriptor;
}

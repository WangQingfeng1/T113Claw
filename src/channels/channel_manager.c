/*
 * T113Claw Channel Manager — unified channel registry and dispatch
 */

#include "channel_manager.h"
#include "utils/log.h"
#include "utils/utils.h"

#include <string.h>

#define TAG "chan_mgr"

static const mc_channel_t *s_channels[T113CLAW_MAX_CHANNELS];
static int s_count = 0;

/* ── Public API ───────────────────────────────────────────── */

int channel_manager_init(void)
{
    s_count = 0;
    memset(s_channels, 0, sizeof(s_channels));
    LOG_I(TAG, "Channel manager initialized");
    return MC_OK;
}

int channel_manager_register(const mc_channel_t *ch)
{
    if (!ch || !ch->name) {
        LOG_E(TAG, "Cannot register NULL channel");
        return MC_ERR;
    }
    if (s_count >= T113CLAW_MAX_CHANNELS) {
        LOG_E(TAG, "Channel registry full (%d)", T113CLAW_MAX_CHANNELS);
        return MC_ERR;
    }

    /* Check for duplicate */
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_channels[i]->name, ch->name) == 0) {
            LOG_W(TAG, "Channel '%s' already registered", ch->name);
            return MC_OK;
        }
    }

    s_channels[s_count++] = ch;
    LOG_I(TAG, "Registered channel: %s (%d/%d)", ch->name, s_count, T113CLAW_MAX_CHANNELS);
    return MC_OK;
}

int channel_manager_init_all(void)
{
    for (int i = 0; i < s_count; i++) {
        if (s_channels[i]->init) {
            int rc = s_channels[i]->init();
            if (rc != MC_OK) {
                LOG_E(TAG, "Channel '%s' init failed (%d)", s_channels[i]->name, rc);
                return rc;
            }
        }
    }
    return MC_OK;
}

int channel_manager_start_all(void)
{
    for (int i = 0; i < s_count; i++) {
        if (s_channels[i]->start) {
            int rc = s_channels[i]->start();
            if (rc != MC_OK) {
                LOG_W(TAG, "Channel '%s' start failed (%d)", s_channels[i]->name, rc);
                /* Non-fatal: continue starting others */
            }
        }
    }
    return MC_OK;
}

void channel_manager_stop_all(void)
{
    /* Stop in reverse order */
    for (int i = s_count - 1; i >= 0; i--) {
        if (s_channels[i]->stop) {
            s_channels[i]->stop();
            LOG_D(TAG, "Stopped channel: %s", s_channels[i]->name);
        }
    }
}

int channel_manager_send(const char *channel, const char *chat_id, const char *text)
{
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_channels[i]->name, channel) == 0) {
            if (s_channels[i]->send) {
                return s_channels[i]->send(chat_id, text);
            }
            LOG_W(TAG, "Channel '%s' has no send function", channel);
            return MC_ERR;
        }
    }

    LOG_W(TAG, "Unknown channel: %s", channel);
    return MC_ERR_NOTFOUND;
}

const mc_channel_t *channel_manager_get(const char *name)
{
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_channels[i]->name, name) == 0) {
            return s_channels[i];
        }
    }
    return NULL;
}

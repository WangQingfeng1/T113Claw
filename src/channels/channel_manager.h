#pragma once
/*
 * T113Claw Channel Manager
 *
 * Unified channel registry: all channels register via mc_channel_t descriptor.
 * Main loop dispatches outbound messages through channel_manager_send().
 */

#include "channel.h"

/* Maximum number of channels that can be registered */
#define T113CLAW_MAX_CHANNELS 8

/* Initialize the channel manager */
int channel_manager_init(void);

/* Register a channel descriptor. Returns MC_OK or MC_ERR. */
int channel_manager_register(const mc_channel_t *ch);

/* Initialize all registered channels (calls each channel's init) */
int channel_manager_init_all(void);

/* Start all registered channels */
int channel_manager_start_all(void);

/* Stop all registered channels (in reverse order) */
void channel_manager_stop_all(void);

/* Route an outbound message to the appropriate channel by name.
 * Returns MC_OK if dispatched, MC_ERR_NOTFOUND if no matching channel. */
int channel_manager_send(const char *channel, const char *chat_id, const char *text);

/* Get a channel descriptor by name. Returns NULL if not found. */
const mc_channel_t *channel_manager_get(const char *name);

#pragma once
/*
 * Voice Channel — wraps voice_manager as a unified channel
 */

#include "channels/channel.h"

/* Get the voice channel descriptor for channel_manager registration */
const mc_channel_t *voice_channel_descriptor(void);

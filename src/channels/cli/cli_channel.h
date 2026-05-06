#pragma once
/*
 * CLI Channel — stdin/stdout text interaction for development
 */

#include "channels/channel.h"

int cli_channel_init(void);
int cli_channel_start(void);
void cli_channel_stop(void);
int cli_channel_send(const char *chat_id, const char *text);

/* Get the CLI channel descriptor for channel_manager registration */
const mc_channel_t *cli_channel_descriptor(void);

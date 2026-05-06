#pragma once
/*
 * T113Claw Feishu Bot Channel
 *
 * Connects to Feishu via WebSocket long connection (persistent connection mode).
 * No inbound port exposure needed — device initiates connection to Feishu servers.
 */

#include "channels/channel.h"

/* Initialize Feishu bot (loads credentials, prepares state) */
int feishu_bot_init(void);

/* Start Feishu bot (launches WS polling thread) */
int feishu_bot_start(void);

/* Stop Feishu bot */
void feishu_bot_stop(void);

/* Send a text message to a Feishu user or group chat */
int feishu_bot_send(const char *chat_id, const char *text);

/* Get the Feishu channel descriptor for channel_manager registration */
const mc_channel_t *feishu_bot_descriptor(void);

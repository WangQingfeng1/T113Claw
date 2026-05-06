#pragma once
/*
 * T113Claw Message Bus
 *
 * Thread-safe dual-queue message bus for channel ↔ agent communication.
 * Replaces FreeRTOS queues with POSIX pthread mutex + condvar ring buffers.
 */

#include <stdint.h>
#include <stddef.h>
#include "t113claw_config.h"

/* Message on the bus */
typedef struct {
    char channel[16];       /* source/destination channel */
    char chat_id[96];       /* per-user/per-group session id */
    char *content;          /* heap-allocated message text (bus takes ownership) */
} mc_msg_t;

/* Initialize the message bus (inbound + outbound queues) */
int message_bus_init(void);

/* Destroy the message bus */
void message_bus_destroy(void);

/* Push a message to the inbound queue (towards Agent Loop).
 * The bus takes ownership of msg->content. */
int message_bus_push_inbound(const mc_msg_t *msg);

/* Pop a message from the inbound queue (blocking with timeout).
 * Caller must free msg->content when done.
 * timeout_ms = 0 means wait forever. */
int message_bus_pop_inbound(mc_msg_t *msg, uint32_t timeout_ms);

/* Push a message to the outbound queue (towards channels).
 * The bus takes ownership of msg->content. */
int message_bus_push_outbound(const mc_msg_t *msg);

/* Pop a message from the outbound queue (blocking with timeout).
 * Caller must free msg->content when done. */
int message_bus_pop_outbound(mc_msg_t *msg, uint32_t timeout_ms);

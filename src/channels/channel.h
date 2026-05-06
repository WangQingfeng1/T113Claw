#pragma once
/*
 * T113Claw Channel Abstraction
 *
 * Each channel receives messages from an external source
 * and pushes them to the message bus.
 */

typedef struct {
    const char *name;
    int (*init)(void);
    int (*start)(void);
    void (*stop)(void);
    int (*send)(const char *chat_id, const char *text);
} mc_channel_t;

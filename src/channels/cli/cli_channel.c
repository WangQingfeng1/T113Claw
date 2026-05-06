#include "cli_channel.h"
#include "channels/channel.h"
#include "bus/message_bus.h"
#include "t113claw_config.h"
#include "utils/log.h"
#include "utils/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>

#define TAG "cli"
#define CLI_CHAT_ID "local"

static pthread_t s_thread;
static volatile bool s_running = false;

static void *cli_read_thread(void *arg)
{
    (void)arg;
    char line[2048];

    LOG_I(TAG, "CLI channel ready. Type a message and press Enter.");
    printf("\n> ");
    fflush(stdout);

    while (s_running) {
        if (!fgets(line, sizeof(line), stdin)) {
            break;  /* EOF or error */
        }

        /* Trim newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (len == 0) {
            printf("> ");
            fflush(stdout);
            continue;
        }

        /* Handle special commands */
        if (strcmp(line, "/quit") == 0 || strcmp(line, "/exit") == 0) {
            LOG_I(TAG, "CLI exit requested");
            s_running = false;
            break;
        }
        if (strcmp(line, "/clear") == 0) {
            /* TODO: session_clear(CLI_CHAT_ID); */
            printf("[Session cleared]\n> ");
            fflush(stdout);
            continue;
        }

        /* Push to inbound queue */
        mc_msg_t msg = {0};
        snprintf(msg.channel, sizeof(msg.channel), "%s", T113CLAW_CHAN_CLI);
        snprintf(msg.chat_id, sizeof(msg.chat_id), "%s", CLI_CHAT_ID);
        msg.content = strdup(line);

        message_bus_push_inbound(&msg);
    }

    LOG_I(TAG, "CLI read thread exited");
    return NULL;
}

/* ── Public API ───────────────────────────────────────────── */

int cli_channel_init(void)
{
    LOG_I(TAG, "CLI channel initialized");
    return MC_OK;
}

int cli_channel_start(void)
{
    s_running = true;
    int rc = pthread_create(&s_thread, NULL, cli_read_thread, NULL);
    if (rc != 0) {
        LOG_E(TAG, "Failed to create CLI thread");
        return MC_ERR;
    }
    pthread_setname_np(s_thread, "cli_channel");
    return MC_OK;
}

void cli_channel_stop(void)
{
    s_running = false;
    /* Note: fgets blocks, so thread may not exit immediately */
    pthread_cancel(s_thread);
    pthread_join(s_thread, NULL);
}

int cli_channel_send(const char *chat_id, const char *text)
{
    (void)chat_id;
    printf("\n🤖 %s\n\n> ", text);
    fflush(stdout);
    return MC_OK;
}

/* ── Channel descriptor ───────────────────────────────────── */

static const mc_channel_t s_cli_descriptor = {
    .name  = T113CLAW_CHAN_CLI,
    .init  = cli_channel_init,
    .start = cli_channel_start,
    .stop  = cli_channel_stop,
    .send  = cli_channel_send,
};

const mc_channel_t *cli_channel_descriptor(void)
{
    return &s_cli_descriptor;
}

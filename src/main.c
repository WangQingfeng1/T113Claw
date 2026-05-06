/*
 * T113Claw — Main Entry Point
 *
 * Initialization sequence:
 * 1. Config/dirs/light prerequisites → 2. UI init/main loop →
 * 3. Background bootstrap for network/core/channels/services
 */

#include "t113claw_config.h"
#include "config/config.h"
#include "http_client.h"
#include "llm/llm_client.h"
#include "utils/log.h"
#include "utils/utils.h"
#include "bus/message_bus.h"
#include "memory/memory_store.h"
#include "memory/session_mgr.h"
#include "skills/skill_loader.h"
#include "tools/tool_registry.h"
#include "agent/agent_loop.h"
#include "channels/channel_manager.h"
#include "channels/cli/cli_channel.h"
#include "channels/feishu/feishu_bot.h"
#include "channels/voice/voice_channel.h"
#include "services/wifi_service.h"
#include "services/cron_service.h"
#include "services/heartbeat.h"
#include "services/audio_service.h"
#include "ui/ui_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#define TAG "main"

/* Define the global log level (declared extern in log.h) */
log_level_t g_log_level = LOG_LEVEL_INFO;

static volatile bool s_running = true;
static pthread_t s_bootstrap_thread;
static volatile bool s_bootstrap_started = false;
static volatile bool s_agent_started = false;
static volatile bool s_audio_started = false;
static volatile bool s_channels_started = false;
static volatile bool s_cron_started = false;
static volatile bool s_heartbeat_started = false;

static void signal_handler(int sig)
{
    (void)sig;
    s_running = false;
}

/* ── Outbound dispatcher ──────────────────────────────────── */
/*
 * Polls outbound message bus and routes to the appropriate channel
 * via channel_manager. Runs in the main thread.
 */
static void dispatch_outbound(void)
{
    mc_msg_t msg;
    int rc = message_bus_pop_outbound(&msg, 5);
    if (rc != MC_OK) return;

    LOG_I(TAG, "Dispatching [%s/%s]: %.80s",
           msg.channel, msg.chat_id, msg.content);

    ui_manager_notify_assistant_message(msg.channel, msg.content);

    rc = channel_manager_send(msg.channel, msg.chat_id, msg.content);
    if (rc != MC_OK) {
        LOG_W(TAG, "Failed to dispatch to channel '%s'", msg.channel);
    }

    free(msg.content);
}

static void *bootstrap_thread_main(void *arg)
{
    (void)arg;

    /* Network remains best-effort; UI should not wait for it. */
    wifi_service_start();

    http_client_init();
    llm_client_init();
    memory_store_init();
    session_mgr_init();
    skill_loader_init();
    tool_registry_init();
    agent_loop_init();

    channel_manager_init();
    channel_manager_register(cli_channel_descriptor());
    channel_manager_register(feishu_bot_descriptor());
    channel_manager_register(voice_channel_descriptor());
    channel_manager_init_all();

    cron_service_init();
    heartbeat_init();
    audio_service_init();

    LOG_I(TAG, "Starting subsystems...");

    if (agent_loop_start() == MC_OK) {
        s_agent_started = true;
    }

    if (audio_service_start() == MC_OK) {
        s_audio_started = true;
    }

    if (channel_manager_start_all() == MC_OK) {
        s_channels_started = true;
    }

    if (cron_service_start() == MC_OK) {
        s_cron_started = true;
    }

    if (heartbeat_start() == MC_OK) {
        s_heartbeat_started = true;
    }

    LOG_I(TAG, "T113Claw runtime bootstrap complete.");
    return NULL;
}

/* ── Main ─────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    /* Set timezone */
    setenv("TZ", T113CLAW_TIMEZONE, 1);
    tzset();

    /* Daemonize if -d flag is passed (for ADB remote start) */
    int daemon_mode = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            daemon_mode = 1;
        }
    }
    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return 1; }
        if (pid > 0) { return 0; } /* parent exits */
        setsid(); /* new session */
    }

    printf("┌─────────────────────────────────────┐\n"
           "│  T113Claw v%s — Micro Agent         │\n"
           "│  T113-S3 Linux AI Assistant         │\n"
           "└─────────────────────────────────────┘\n\n",
           T113CLAW_VERSION);

    /* Signal handling */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    /* 1. Config */
    const char *config_path = T113CLAW_DATA_DIR "/config/config.ini";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") != 0) { config_path = argv[i]; break; }
    }

    config_init(config_path);
    LOG_I(TAG, "Config loaded from %s", config_path);

    /* 2. Create data directories */
    mc_ensure_dir(T113CLAW_DATA_DIR);
    mc_ensure_dir(T113CLAW_DATA_DIR "/config");
    mc_ensure_dir(T113CLAW_DATA_DIR "/memory");
    mc_ensure_dir(T113CLAW_DATA_DIR "/sessions");
    mc_ensure_dir(T113CLAW_DATA_DIR "/skills");
    mc_ensure_dir(T113CLAW_DATA_DIR "/ui");
    mc_ensure_dir(T113CLAW_DATA_DIR "/ui/font");
    mc_ensure_dir(T113CLAW_DATA_DIR "/ui/image");

    /* 3. Early UI prerequisites */
    wifi_service_init();
    message_bus_init();

    /* 4. UI must come up before any slow/network/hardware bootstrap work. */
    ui_manager_init();

    if (pthread_create(&s_bootstrap_thread, NULL, bootstrap_thread_main, NULL) == 0) {
        s_bootstrap_started = true;
    } else {
        LOG_E(TAG, "Failed to create bootstrap thread");
        s_running = false;
    }

    LOG_I(TAG, "UI is live. Background bootstrap is running.\n");

    /* ── Main loop: dispatch outbound messages ─────────────── */

    while (s_running) {
        ui_manager_update();
        dispatch_outbound();
        usleep(5000);
    }

    /* ── Shutdown ──────────────────────────────────────────── */

    LOG_I(TAG, "Shutting down...");

    if (s_bootstrap_started) {
        pthread_join(s_bootstrap_thread, NULL);
        s_bootstrap_started = false;
    }

    if (s_channels_started) channel_manager_stop_all();
    if (s_audio_started) audio_service_stop();
    if (s_heartbeat_started) heartbeat_stop();
    if (s_cron_started) cron_service_stop();
    wifi_service_stop();
    if (s_agent_started) agent_loop_stop();
    ui_manager_shutdown();

    http_client_cleanup();
    message_bus_destroy();

    LOG_I(TAG, "T113Claw stopped. Goodbye.");
    return 0;
}

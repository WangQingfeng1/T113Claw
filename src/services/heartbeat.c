#include "heartbeat.h"
#include "t113claw_config.h"
#include "utils/log.h"
#include "utils/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/sysinfo.h>

#define TAG "heartbeat"

static pthread_t s_thread;
static volatile bool s_running = false;

static void *heartbeat_thread(void *arg)
{
    (void)arg;
    LOG_I(TAG, "Heartbeat started");

    while (s_running) {
        struct sysinfo si;
        if (sysinfo(&si) == 0) {
            double load = si.loads[0] / 65536.0;
            unsigned long free_mb = si.freeram * si.mem_unit / (1024 * 1024);
            LOG_D(TAG, "Heartbeat — free: %lu MB, load: %.2f, uptime: %lds",
                   free_mb, load, si.uptime);
        }

        /* Sleep 60 seconds */
        struct timespec ts = {.tv_sec = 60, .tv_nsec = 0};
        nanosleep(&ts, NULL);
    }

    return NULL;
}

int heartbeat_init(void)
{
    LOG_I(TAG, "Heartbeat initialized");
    return MC_OK;
}

int heartbeat_start(void)
{
    s_running = true;
    int rc = pthread_create(&s_thread, NULL, heartbeat_thread, NULL);
    if (rc != 0) {
        LOG_E(TAG, "Failed to create heartbeat thread");
        return MC_ERR;
    }
    pthread_setname_np(s_thread, "heartbeat");
    return MC_OK;
}

void heartbeat_stop(void)
{
    s_running = false;
    pthread_join(s_thread, NULL);
}

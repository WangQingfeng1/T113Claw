#include "cron_service.h"
#include "bus/message_bus.h"
#include "t113claw_config.h"
#include "utils/log.h"
#include "utils/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#define TAG "cron"
#define MAX_JOBS 32

static cron_job_t s_jobs[MAX_JOBS];
static int s_job_count = 0;
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t s_thread;
static volatile bool s_running = false;
static int s_next_id = 1;

/* ── Cron thread ──────────────────────────────────────────── */

static void fire_job(cron_job_t *job)
{
    LOG_I(TAG, "Firing job '%s': %s", job->name, job->message);

    mc_msg_t msg = {0};
    snprintf(msg.channel, sizeof(msg.channel), "%s",
             job->channel[0] ? job->channel : T113CLAW_CHAN_CLI);
    snprintf(msg.chat_id, sizeof(msg.chat_id), "%s",
             job->chat_id[0] ? job->chat_id : "cron");
    msg.content = strdup(job->message);
    message_bus_push_inbound(&msg);
}

static void *cron_thread(void *arg)
{
    (void)arg;
    LOG_I(TAG, "Cron service started");

    while (s_running) {
        time_t now = time(NULL);

        pthread_mutex_lock(&s_mutex);
        for (int i = 0; i < s_job_count; ) {
            cron_job_t *j = &s_jobs[i];
            bool should_fire = false;
            bool should_remove = false;

            if (j->type == CRON_EVERY) {
                if (j->last_fire == 0 || (now - j->last_fire) >= j->interval_s) {
                    should_fire = true;
                    j->last_fire = now;
                }
            } else if (j->type == CRON_AT) {
                if (now >= j->at_epoch) {
                    should_fire = true;
                    should_remove = true;
                }
            }

            if (should_fire) {
                fire_job(j);
            }

            if (should_remove) {
                /* Remove by swapping with last */
                s_jobs[i] = s_jobs[--s_job_count];
            } else {
                i++;
            }
        }
        pthread_mutex_unlock(&s_mutex);

        /* Check every 5 seconds */
        struct timespec ts = {.tv_sec = 5, .tv_nsec = 0};
        nanosleep(&ts, NULL);
    }

    LOG_I(TAG, "Cron service stopped");
    return NULL;
}

/* ── Public API ───────────────────────────────────────────── */

int cron_service_init(void)
{
    s_job_count = 0;
    s_next_id = 1;
    LOG_I(TAG, "Cron service initialized");
    return MC_OK;
}

int cron_service_start(void)
{
    s_running = true;
    int rc = pthread_create(&s_thread, NULL, cron_thread, NULL);
    if (rc != 0) {
        LOG_E(TAG, "Failed to create cron thread");
        return MC_ERR;
    }
    pthread_setname_np(s_thread, "cron_svc");
    return MC_OK;
}

void cron_service_stop(void)
{
    s_running = false;
    pthread_join(s_thread, NULL);
}

int cron_add_job(cron_job_t *job)
{
    pthread_mutex_lock(&s_mutex);

    if (s_job_count >= MAX_JOBS) {
        pthread_mutex_unlock(&s_mutex);
        return MC_ERR;
    }

    snprintf(job->id, sizeof(job->id), "job_%d", s_next_id++);
    job->last_fire = 0;

    s_jobs[s_job_count++] = *job;

    pthread_mutex_unlock(&s_mutex);

    LOG_I(TAG, "Added job '%s' (id=%s, type=%s)",
           job->name, job->id,
           job->type == CRON_EVERY ? "every" : "at");
    return MC_OK;
}

int cron_list_jobs(char *output, size_t size)
{
    pthread_mutex_lock(&s_mutex);

    if (s_job_count == 0) {
        snprintf(output, size, "No scheduled jobs.");
        pthread_mutex_unlock(&s_mutex);
        return MC_OK;
    }

    size_t off = 0;
    for (int i = 0; i < s_job_count && off < size - 128; i++) {
        cron_job_t *j = &s_jobs[i];
        if (j->type == CRON_EVERY) {
            off += snprintf(output + off, size - off,
                            "[%s] '%s' every %ds — %s\n",
                            j->id, j->name, j->interval_s, j->message);
        } else {
            off += snprintf(output + off, size - off,
                            "[%s] '%s' at %ld — %s\n",
                            j->id, j->name, (long)j->at_epoch, j->message);
        }
    }

    pthread_mutex_unlock(&s_mutex);
    return MC_OK;
}

int cron_remove_job(const char *job_id)
{
    pthread_mutex_lock(&s_mutex);

    for (int i = 0; i < s_job_count; i++) {
        if (strcmp(s_jobs[i].id, job_id) == 0) {
            LOG_I(TAG, "Removing job '%s'", s_jobs[i].name);
            s_jobs[i] = s_jobs[--s_job_count];
            pthread_mutex_unlock(&s_mutex);
            return MC_OK;
        }
    }

    pthread_mutex_unlock(&s_mutex);
    return MC_ERR_NOTFOUND;
}

#pragma once
/*
 * T113Claw Cron Service
 *
 * Schedules recurring and one-shot jobs.
 * When a job fires, it pushes a message to the inbound queue.
 */

#include <stddef.h>
#include <time.h>

typedef enum {
    CRON_EVERY,  /* Recurring at interval */
    CRON_AT,     /* One-shot at specific time */
} cron_type_t;

typedef struct {
    char id[16];
    char name[64];
    cron_type_t type;
    int interval_s;
    time_t at_epoch;
    char message[256];
    char channel[16];
    char chat_id[96];
    time_t last_fire;
} cron_job_t;

int cron_service_init(void);
int cron_service_start(void);
void cron_service_stop(void);

/* Tool interface */
int cron_add_job(cron_job_t *job);
int cron_list_jobs(char *output, size_t size);
int cron_remove_job(const char *job_id);

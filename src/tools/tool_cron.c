/*
 * Tool: cron_add, cron_list, cron_remove
 *
 * Delegates to cron_service for actual scheduling.
 */

#include "tool_registry.h"
#include "services/cron_service.h"
#include "utils/utils.h"

#include <stdio.h>
#include <string.h>
#include <cJSON.h>

int tool_cron_add_execute(const char *input, char *output, size_t size)
{
    cJSON *root = cJSON_Parse(input);
    if (!root) {
        snprintf(output, size, "Error: invalid JSON");
        return -1;
    }

    cJSON *j_name     = cJSON_GetObjectItem(root, "name");
    cJSON *j_type     = cJSON_GetObjectItem(root, "schedule_type");
    cJSON *j_interval = cJSON_GetObjectItem(root, "interval_s");
    cJSON *j_epoch    = cJSON_GetObjectItem(root, "at_epoch");
    cJSON *j_msg      = cJSON_GetObjectItem(root, "message");
    cJSON *j_chan      = cJSON_GetObjectItem(root, "channel");
    cJSON *j_chat      = cJSON_GetObjectItem(root, "chat_id");

    if (!j_name || !j_type || !j_msg) {
        snprintf(output, size, "Error: missing required fields");
        cJSON_Delete(root);
        return -1;
    }

    cron_job_t job = {0};
    snprintf(job.name, sizeof(job.name), "%s", j_name->valuestring);
    snprintf(job.message, sizeof(job.message), "%s", j_msg->valuestring);

    if (j_chan && j_chan->valuestring)
        snprintf(job.channel, sizeof(job.channel), "%s", j_chan->valuestring);
    if (j_chat && j_chat->valuestring)
        snprintf(job.chat_id, sizeof(job.chat_id), "%s", j_chat->valuestring);

    if (strcmp(j_type->valuestring, "every") == 0) {
        job.type = CRON_EVERY;
        job.interval_s = j_interval ? j_interval->valueint : 60;
    } else {
        job.type = CRON_AT;
        job.at_epoch = j_epoch ? (time_t)j_epoch->valuedouble : 0;
    }

    int rc = cron_add_job(&job);
    if (rc == 0) {
        snprintf(output, size, "Job '%s' scheduled (id: %s)", job.name, job.id);
    } else {
        snprintf(output, size, "Error: failed to add job");
    }

    cJSON_Delete(root);
    return rc;
}

int tool_cron_list_execute(const char *input, char *output, size_t size)
{
    (void)input;
    return cron_list_jobs(output, size);
}

int tool_cron_remove_execute(const char *input, char *output, size_t size)
{
    cJSON *root = cJSON_Parse(input);
    if (!root) {
        snprintf(output, size, "Error: invalid JSON");
        return -1;
    }

    cJSON *j_id = cJSON_GetObjectItem(root, "job_id");
    if (!j_id || !j_id->valuestring) {
        snprintf(output, size, "Error: missing 'job_id'");
        cJSON_Delete(root);
        return -1;
    }

    int rc = cron_remove_job(j_id->valuestring);
    if (rc == 0) {
        snprintf(output, size, "Job '%s' removed", j_id->valuestring);
    } else {
        snprintf(output, size, "Error: job not found");
    }

    cJSON_Delete(root);
    return rc;
}

#include "tool_remote_exec.h"

#include "t113claw_config.h"
#include "services/remote_client.h"
#include "utils/log.h"
#include "utils/utils.h"

#include <cJSON.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define TAG "tool_remote"

typedef struct {
    char command[REMOTE_CLIENT_COMMAND_LEN];
    char working_directory[REMOTE_CLIENT_WORKDIR_LEN];
    int timeout_s;
} remote_request_t;

static int appendf(char *output, size_t output_size, size_t *off,
                   const char *fmt, ...)
{
    if (*off >= output_size) {
        return -1;
    }

    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(output + *off, output_size - *off, fmt, ap);
    va_end(ap);

    if (written < 0) {
        return -1;
    }

    if ((size_t)written >= output_size - *off) {
        *off = output_size - 1;
        return -1;
    }

    *off += (size_t)written;
    return 0;
}

static int parse_request(const char *input, remote_request_t *req,
                         char *output, size_t output_size)
{
    memset(req, 0, sizeof(*req));
    req->timeout_s = T113CLAW_REMOTE_DEFAULT_TIMEOUT_S;

    cJSON *root = cJSON_Parse(input);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return MC_ERR;
    }

    cJSON *command_j = cJSON_GetObjectItem(root, "command");
    if (!command_j || !cJSON_IsString(command_j) || !command_j->valuestring[0]) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: missing 'command'");
        return MC_ERR;
    }

    if (strlen(command_j->valuestring) >= sizeof(req->command)) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: command too long (max %zu chars)",
                 sizeof(req->command) - 1);
        return MC_ERR;
    }
    snprintf(req->command, sizeof(req->command), "%s", command_j->valuestring);

    cJSON *timeout_j = cJSON_GetObjectItem(root, "timeout");
    if (timeout_j && cJSON_IsNumber(timeout_j)) {
        req->timeout_s = timeout_j->valueint;
    }
    if (req->timeout_s <= 0) {
        req->timeout_s = T113CLAW_REMOTE_DEFAULT_TIMEOUT_S;
    }
    if (req->timeout_s > T113CLAW_REMOTE_MAX_TIMEOUT_S) {
        req->timeout_s = T113CLAW_REMOTE_MAX_TIMEOUT_S;
    }

    cJSON *workdir_j = cJSON_GetObjectItem(root, "working_directory");
    if (workdir_j && cJSON_IsString(workdir_j) && workdir_j->valuestring[0]) {
        if (strlen(workdir_j->valuestring) >= sizeof(req->working_directory)) {
            cJSON_Delete(root);
            snprintf(output, output_size, "Error: working_directory too long (max %zu chars)",
                     sizeof(req->working_directory) - 1);
            return MC_ERR;
        }
        snprintf(req->working_directory, sizeof(req->working_directory), "%s",
                 workdir_j->valuestring);
    }

    cJSON_Delete(root);
    return MC_OK;
}
static int format_response(const remote_exec_result_t *result,
                           char *output, size_t output_size)
{
    size_t off = 0;
    appendf(output, output_size, &off, "Remote target: %s\n", result->target);
    appendf(output, output_size, &off, "Exit code: %d\n", result->exit_code);
    if (result->duration_ms >= 0) {
        appendf(output, output_size, &off, "Duration: %d ms\n", result->duration_ms);
    }
    appendf(output, output_size, &off, "Timed out: %s\n", result->timed_out ? "yes" : "no");

    if (result->stdout_text[0]) {
        appendf(output, output_size, &off, "\nStdout:\n%s\n", result->stdout_text);
        if (result->stdout_truncated) {
            appendf(output, output_size, &off, "(stdout truncated)\n");
        }
    }
    if (result->stderr_text[0]) {
        appendf(output, output_size, &off, "\nStderr:\n%s\n", result->stderr_text);
        if (result->stderr_truncated) {
            appendf(output, output_size, &off, "(stderr truncated)\n");
        }
    }
    if (!result->stdout_text[0] && !result->stderr_text[0]) {
        appendf(output, output_size, &off, "\n(no output)\n");
    }

    return MC_OK;
}

int tool_remote_exec_execute(const char *input, char *output, size_t size)
{
    remote_request_t req;
    remote_exec_result_t result;
    char error[256];

    if (parse_request(input, &req, output, size) != MC_OK) {
        return MC_ERR;
    }

    if (remote_client_exec(req.command,
                           req.working_directory,
                           req.timeout_s,
                           &result,
                           error,
                           sizeof(error)) != MC_OK) {
        snprintf(output, size, "Error: %s", error);
        return MC_ERR;
    }

    format_response(&result, output, size);
    return MC_OK;
}
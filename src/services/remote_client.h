#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "t113claw_config.h"

#define REMOTE_CLIENT_TARGET_LEN   256
#define REMOTE_CLIENT_HOSTNAME_LEN 128
#define REMOTE_CLIENT_PLATFORM_LEN 192
#define REMOTE_CLIENT_CWD_LEN      256
#define REMOTE_CLIENT_USER_LEN     64
#define REMOTE_CLIENT_COMMAND_LEN  512
#define REMOTE_CLIENT_WORKDIR_LEN  256

typedef struct {
    char target[REMOTE_CLIENT_TARGET_LEN];
    char hostname[REMOTE_CLIENT_HOSTNAME_LEN];
    char platform[REMOTE_CLIENT_PLATFORM_LEN];
    char cwd[REMOTE_CLIENT_CWD_LEN];
    char user[REMOTE_CLIENT_USER_LEN];
} remote_status_t;

typedef struct {
    char target[REMOTE_CLIENT_TARGET_LEN];
    char command[REMOTE_CLIENT_COMMAND_LEN];
    int exit_code;
    int duration_ms;
    bool timed_out;
    bool stdout_truncated;
    bool stderr_truncated;
    char stdout_text[T113CLAW_REMOTE_OUTPUT_TEXT_MAX];
    char stderr_text[T113CLAW_REMOTE_OUTPUT_TEXT_MAX];
} remote_exec_result_t;

int remote_client_status(remote_status_t *status, char *error, size_t error_size);
int remote_client_exec(const char *command,
                       const char *working_directory,
                       int timeout_s,
                       remote_exec_result_t *result,
                       char *error,
                       size_t error_size);
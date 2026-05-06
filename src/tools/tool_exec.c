/*
 * Tool: run_command — Execute shell commands on the device
 *
 * Enables the LLM to:
 *   - Run arbitrary shell commands
 *   - Write code → compile → execute → iterate
 *   - Read GPIO, control hardware, manage services
 *   - Install packages, modify system config
 *
 * Safety:
 *   - Default timeout 30s, max 120s
 *   - Output truncated to fit tool output buffer
 *   - Uses fork + waitpid with alarm for timeout (no external `timeout` needed)
 *   - Captures both stdout and stderr
 */

#include "tool_registry.h"
#include "t113claw_config.h"
#include "utils/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <cJSON.h>

#define TAG "tool_exec"

#define DEFAULT_TIMEOUT_S   30
#define MAX_TIMEOUT_S       120

/* SIGALRM handler for timeout */
static volatile sig_atomic_t s_timed_out = 0;

static void alarm_handler(int sig)
{
    (void)sig;
    s_timed_out = 1;
}

int tool_exec_execute(const char *input, char *output, size_t size)
{
    cJSON *root = cJSON_Parse(input);
    if (!root) {
        snprintf(output, size, "Error: invalid JSON input");
        return -1;
    }

    cJSON *cmd_j = cJSON_GetObjectItem(root, "command");
    if (!cmd_j || !cmd_j->valuestring || !cmd_j->valuestring[0]) {
        snprintf(output, size, "Error: missing 'command'");
        cJSON_Delete(root);
        return -1;
    }

    int timeout_s = DEFAULT_TIMEOUT_S;
    cJSON *timeout_j = cJSON_GetObjectItem(root, "timeout");
    if (timeout_j && cJSON_IsNumber(timeout_j)) {
        timeout_s = timeout_j->valueint;
        if (timeout_s <= 0) timeout_s = DEFAULT_TIMEOUT_S;
        if (timeout_s > MAX_TIMEOUT_S) timeout_s = MAX_TIMEOUT_S;
    }

    const char *workdir = NULL;
    cJSON *workdir_j = cJSON_GetObjectItem(root, "working_directory");
    if (workdir_j && cJSON_IsString(workdir_j) && workdir_j->valuestring[0]) {
        workdir = workdir_j->valuestring;
    }

    const char *cmd = cmd_j->valuestring;
    LOG_I(TAG, "Executing: %.120s (timeout=%ds)", cmd, timeout_s);

    /* Create pipe for capturing output */
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        snprintf(output, size, "Error: failed to create pipe");
        cJSON_Delete(root);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        snprintf(output, size, "Error: fork failed");
        cJSON_Delete(root);
        return -1;
    }

    if (pid == 0) {
        /* Child process */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        if (workdir && chdir(workdir) != 0) {
            fprintf(stderr, "Error: cannot cd to '%s': %s\n", workdir, strerror(errno));
            _exit(127);
        }

        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    /* Parent process */
    close(pipefd[1]);

    /* Set up timeout via SIGALRM */
    s_timed_out = 0;
    struct sigaction sa, old_sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = alarm_handler;
    sa.sa_flags = 0; /* No SA_RESTART — so read/waitpid get interrupted */
    sigaction(SIGALRM, &sa, &old_sa);
    alarm(timeout_s);

    /* Read child output */
    size_t reserve = 128;
    size_t max_read = (size > reserve) ? size - reserve : 0;
    size_t off = 0;
    int truncated = 0;
    char buf[1024];
    ssize_t n;

    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        if (!truncated && off + (size_t)n < max_read) {
            memcpy(output + off, buf, (size_t)n);
            off += (size_t)n;
        } else {
            truncated = 1;
        }
    }
    close(pipefd[0]);

    /* Wait for child */
    int status = 0;
    int timed_out = 0;

    if (waitpid(pid, &status, WNOHANG) == 0) {
        /* Child still running */
        if (s_timed_out) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            timed_out = 1;
        } else {
            waitpid(pid, &status, 0);
        }
    }

    /* Cancel alarm and restore handler */
    alarm(0);
    sigaction(SIGALRM, &old_sa, NULL);

    int exit_code = -1;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_code = 128 + WTERMSIG(status);
    }

    /* Append status info */
    if (truncated) {
        off += snprintf(output + off, size - off,
                        "\n... (output truncated)\n");
    }
    off += snprintf(output + off, size - off,
                    "\n[exit_code: %d]", exit_code);

    if (timed_out) {
        off += snprintf(output + off, size - off,
                        " (command timed out after %ds)", timeout_s);
    }

    output[off] = '\0';

    LOG_I(TAG, "Command finished: exit_code=%d, output_len=%zu%s",
          exit_code, off, timed_out ? " (timeout)" : "");
    cJSON_Delete(root);
    return 0;
}

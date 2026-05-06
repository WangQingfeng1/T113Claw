#include "context_builder.h"
#include "t113claw_config.h"
#include "memory/memory_store.h"
#include "skills/skill_loader.h"
#include "utils/log.h"
#include "utils/utils.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define TAG "context"

int context_build_system_prompt(char *buf, size_t size)
{
    size_t off = 0;
    char tmp[4096];

    /* Current timestamp */
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    off += snprintf(buf + off, size - off,
                    "Current time: %04d-%02d-%02d %02d:%02d:%02d\n\n",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec);

    /* SOUL.md — AI personality */
    if (memory_read_soul(tmp, sizeof(tmp)) == MC_OK && tmp[0]) {
        off += snprintf(buf + off, size - off,
                        "## Identity\n%s\n\n", tmp);
    }

    /* USER.md — User profile */
    if (memory_read_user(tmp, sizeof(tmp)) == MC_OK && tmp[0]) {
        off += snprintf(buf + off, size - off,
                        "## About the User\n%s\n\n", tmp);
    }

    /* MEMORY.md — Long-term facts */
    if (memory_read_long_term(tmp, sizeof(tmp)) == MC_OK && tmp[0]) {
        off += snprintf(buf + off, size - off,
                        "## Long-term Memory\n%s\n\n", tmp);
    }

    /* Recent daily notes */
    if (memory_read_recent(tmp, sizeof(tmp), T113CLAW_MEMORY_RECENT_DAYS) == MC_OK && tmp[0]) {
        off += snprintf(buf + off, size - off,
                        "## Recent Notes\n%s\n\n", tmp);
    }

    /* Skills */
    char skills_buf[4096];
    if (skill_loader_get_all(skills_buf, sizeof(skills_buf)) == MC_OK && skills_buf[0]) {
        off += snprintf(buf + off, size - off,
                        "## Skills\n%s\n\n", skills_buf);
    }

    /* Tool usage guidance */
    off += snprintf(buf + off, size - off,
                    "## Tool Usage Guidelines\n"
                    "- Use get_current_time when asked about the time or date.\n"
                    "- Use read_file/write_file to persist information across conversations.\n"
                    "- Use system_info to check device health.\n"
                    "- Use web_search when the question depends on recent news, external documentation, product info,"
                    " troubleshooting posts, or knowledge that may be missing from local memory.\n"
                    "  Summarize the most relevant results, include useful links, and avoid claiming certainty if search results conflict.\n"
                    "  In auto mode, Tavily is preferred when configured and Sogou is used as a fallback.\n"
                    "- Use cron_add to schedule reminders or recurring tasks.\n"
                    "- Use run_command to execute shell commands on this Linux device.\n"
                    "  You can: read GPIO/sensors, compile & run code, manage services, etc.\n"
                    "  Example workflow: write_file a C program → run_command to compile → run_command to execute.\n"
                    "  This device is an ARM Linux board (T113-S3) running TinaLinux with busybox.\n"
                    "  Available compilers/tools depend on what's installed; use run_command to check first.\n"
                    "- Use remote_exec when the user wants to inspect or control the configured LAN server.\n"
                    "  Prefer non-destructive commands first, and report stdout, stderr, exit code and any timeout clearly.\n"
                    "- Tools return text results. Interpret and relay them naturally.\n"
                    "\n");

    LOG_D(TAG, "System prompt built (%zu bytes)", off);
    return MC_OK;
}

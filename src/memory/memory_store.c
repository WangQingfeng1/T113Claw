#include "memory_store.h"
#include "t113claw_config.h"
#include "utils/log.h"
#include "utils/utils.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define TAG "memory"

/* ── Helpers ──────────────────────────────────────────────── */

static int read_file_into(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        buf[0] = '\0';
        return MC_ERR_NOTFOUND;
    }
    size_t rd = fread(buf, 1, size - 1, f);
    buf[rd] = '\0';
    fclose(f);
    return MC_OK;
}

static void get_today_path(char *buf, size_t size)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    snprintf(buf, size, "%s/%04d-%02d-%02d.md",
             T113CLAW_MEMORY_DIR, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

/* ── Public API ───────────────────────────────────────────── */

int memory_store_init(void)
{
    mc_ensure_dir(T113CLAW_DATA_DIR);
    mc_ensure_dir(T113CLAW_CONFIG_DIR);
    mc_ensure_dir(T113CLAW_MEMORY_DIR);
    mc_ensure_dir(T113CLAW_SESSION_DIR);
    mc_ensure_dir(T113CLAW_SKILLS_DIR);

    LOG_I(TAG, "Memory store initialized (data: %s)", T113CLAW_DATA_DIR);
    return MC_OK;
}

int memory_read_soul(char *buf, size_t size)
{
    return read_file_into(T113CLAW_SOUL_FILE, buf, size);
}

int memory_read_user(char *buf, size_t size)
{
    return read_file_into(T113CLAW_USER_FILE, buf, size);
}

int memory_read_long_term(char *buf, size_t size)
{
    return read_file_into(T113CLAW_MEMORY_FILE, buf, size);
}

int memory_write_long_term(const char *content)
{
    return mc_write_file(T113CLAW_MEMORY_FILE, content);
}

int memory_append_today(const char *note)
{
    char path[256];
    get_today_path(path, sizeof(path));
    return mc_append_file(path, note);
}

int memory_read_recent(char *buf, size_t size, int days)
{
    buf[0] = '\0';
    size_t offset = 0;
    time_t now = time(NULL);

    for (int d = 0; d < days && offset < size - 1; d++) {
        time_t t = now - (d * 86400);
        struct tm tm;
        localtime_r(&t, &tm);

        char path[256];
        snprintf(path, sizeof(path), "%s/%04d-%02d-%02d.md",
                 T113CLAW_MEMORY_DIR, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);

        FILE *f = fopen(path, "r");
        if (!f) continue;

        /* Add date header */
        int n = snprintf(buf + offset, size - offset,
                         "\n--- %04d-%02d-%02d ---\n",
                         tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        if (n > 0) offset += n;

        size_t rd = fread(buf + offset, 1, size - offset - 1, f);
        offset += rd;
        buf[offset] = '\0';
        fclose(f);
    }

    return MC_OK;
}

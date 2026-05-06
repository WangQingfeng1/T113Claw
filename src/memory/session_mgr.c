#include "session_mgr.h"
#include "t113claw_config.h"
#include "utils/log.h"
#include "utils/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cJSON.h>

#define TAG "session"

/* ── Helpers ──────────────────────────────────────────────── */

static void session_path(const char *chat_id, char *buf, size_t size)
{
    /* Sanitize chat_id for filesystem: replace / and .. */
    char safe_id[96];
    snprintf(safe_id, sizeof(safe_id), "%s", chat_id);
    for (char *p = safe_id; *p; p++) {
        if (*p == '/' || *p == '\\') *p = '_';
    }
    snprintf(buf, size, "%s/%s.jsonl", T113CLAW_SESSION_DIR, safe_id);
}

/* ── Public API ───────────────────────────────────────────── */

int session_mgr_init(void)
{
    mc_ensure_dir(T113CLAW_SESSION_DIR);
    LOG_I(TAG, "Session manager initialized");
    return MC_OK;
}

int session_append(const char *chat_id, const char *role, const char *content)
{
    if (!chat_id || !role || !content) return MC_ERR_INVALID;

    char path[256];
    session_path(chat_id, path, sizeof(path));

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "role", role);
    cJSON_AddStringToObject(obj, "content", content);
    char *line = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    if (!line) return MC_ERR_NOMEM;

    FILE *f = fopen(path, "a");
    if (!f) {
        free(line);
        return MC_ERR;
    }
    fprintf(f, "%s\n", line);
    fclose(f);
    free(line);

    return MC_OK;
}

char *session_get_history_json(const char *chat_id, int max_msgs)
{
    char path[256];
    session_path(chat_id, path, sizeof(path));

    char *file_content = mc_read_file(path, NULL);
    if (!file_content) {
        /* No history — return empty array */
        return strdup("[]");
    }

    /* Count total lines */
    int total = 0;
    for (char *p = file_content; *p; p++) {
        if (*p == '\n') total++;
    }

    /* Parse lines from the end (ring buffer effect) */
    int skip = (total > max_msgs) ? (total - max_msgs) : 0;

    cJSON *arr = cJSON_CreateArray();
    char *line = file_content;
    int idx = 0;

    while (line && *line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';

        if (idx >= skip && line[0] == '{') {
            cJSON *obj = cJSON_Parse(line);
            if (obj) {
                cJSON_AddItemToArray(arr, obj);
            }
        }

        idx++;
        line = eol ? eol + 1 : NULL;
    }

    free(file_content);

    char *result = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    return result ? result : strdup("[]");
}

int session_clear(const char *chat_id)
{
    char path[256];
    session_path(chat_id, path, sizeof(path));
    remove(path);
    LOG_I(TAG, "Session cleared: %s", chat_id);
    return MC_OK;
}

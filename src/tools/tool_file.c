/*
 * Tool: read_file, write_file, list_dir
 */

#include "tool_registry.h"
#include "t113claw_config.h"
#include "utils/utils.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <cJSON.h>

/* Resolve path: ensure it stays within data directory */
static int resolve_path(const char *rel, char *abs, size_t size)
{
    if (!rel || rel[0] == '\0') return -1;

    /* Prevent directory traversal */
    if (strstr(rel, "..") != NULL) {
        return -1;
    }

    if (rel[0] == '/') {
        /* Absolute path — must start with T113CLAW_DATA_DIR */
        if (strncmp(rel, T113CLAW_DATA_DIR, strlen(T113CLAW_DATA_DIR)) != 0)
            return -1;
        snprintf(abs, size, "%s", rel);
    } else {
        snprintf(abs, size, "%s/%s", T113CLAW_DATA_DIR, rel);
    }
    return 0;
}

int tool_file_read_execute(const char *input, char *output, size_t size)
{
    cJSON *root = cJSON_Parse(input);
    if (!root) {
        snprintf(output, size, "Error: invalid JSON input");
        return -1;
    }

    cJSON *path = cJSON_GetObjectItem(root, "path");
    if (!path || !path->valuestring) {
        snprintf(output, size, "Error: missing 'path'");
        cJSON_Delete(root);
        return -1;
    }

    char abs[512];
    if (resolve_path(path->valuestring, abs, sizeof(abs)) != 0) {
        snprintf(output, size, "Error: invalid path");
        cJSON_Delete(root);
        return -1;
    }

    char *content = mc_read_file(abs, NULL);
    if (!content) {
        snprintf(output, size, "Error: file not found: %s", path->valuestring);
        cJSON_Delete(root);
        return -1;
    }

    snprintf(output, size, "%s", content);
    free(content);
    cJSON_Delete(root);
    return 0;
}

int tool_file_write_execute(const char *input, char *output, size_t size)
{
    cJSON *root = cJSON_Parse(input);
    if (!root) {
        snprintf(output, size, "Error: invalid JSON input");
        return -1;
    }

    cJSON *path = cJSON_GetObjectItem(root, "path");
    cJSON *content = cJSON_GetObjectItem(root, "content");
    if (!path || !path->valuestring || !content || !content->valuestring) {
        snprintf(output, size, "Error: missing 'path' or 'content'");
        cJSON_Delete(root);
        return -1;
    }

    char abs[512];
    if (resolve_path(path->valuestring, abs, sizeof(abs)) != 0) {
        snprintf(output, size, "Error: invalid path");
        cJSON_Delete(root);
        return -1;
    }

    if (mc_write_file(abs, content->valuestring) != 0) {
        snprintf(output, size, "Error: failed to write file");
        cJSON_Delete(root);
        return -1;
    }

    snprintf(output, size, "File written: %s (%zu bytes)",
             path->valuestring, strlen(content->valuestring));
    cJSON_Delete(root);
    return 0;
}

int tool_file_list_execute(const char *input, char *output, size_t size)
{
    const char *prefix = T113CLAW_DATA_DIR;

    cJSON *root = cJSON_Parse(input);
    char abs[512];
    if (root) {
        cJSON *p = cJSON_GetObjectItem(root, "prefix");
        if (p && p->valuestring && p->valuestring[0]) {
            if (resolve_path(p->valuestring, abs, sizeof(abs)) == 0) {
                prefix = abs;
            }
        }
        cJSON_Delete(root);
    }

    DIR *dir = opendir(prefix);
    if (!dir) {
        snprintf(output, size, "Error: cannot open directory: %s", prefix);
        return -1;
    }

    size_t off = 0;
    off += snprintf(output + off, size - off, "Directory: %s\n", prefix);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && off < size - 64) {
        if (entry->d_name[0] == '.') continue;
        off += snprintf(output + off, size - off, "  %s%s\n",
                        entry->d_name,
                        entry->d_type == DT_DIR ? "/" : "");
    }
    closedir(dir);

    return 0;
}

#include "skill_loader.h"
#include "t113claw_config.h"
#include "utils/log.h"
#include "utils/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#define TAG "skills"

static char s_skills_dir[256];

int skill_loader_init(void)
{
    snprintf(s_skills_dir, sizeof(s_skills_dir), "%s/skills", T113CLAW_DATA_DIR);
    mc_ensure_dir(s_skills_dir);
    LOG_I(TAG, "Skill loader initialized (dir: %s)", s_skills_dir);
    return MC_OK;
}

int skill_loader_get_all(char *buf, size_t size)
{
    DIR *dir = opendir(s_skills_dir);
    if (!dir) {
        buf[0] = '\0';
        return MC_OK;
    }

    size_t off = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL && off < size - 128) {
        /* Only load .md files */
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".md") != 0) continue;

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", s_skills_dir, entry->d_name);

        char *content = mc_read_file(path, NULL);
        if (!content) continue;

        off += snprintf(buf + off, size - off,
                        "### Skill: %s\n%s\n\n", entry->d_name, content);
        free(content);
    }

    closedir(dir);

    if (off == 0) buf[0] = '\0';

    LOG_D(TAG, "Loaded skills (%zu bytes)", off);
    return MC_OK;
}

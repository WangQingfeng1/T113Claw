#pragma once
/*
 * T113Claw Skill Loader
 *
 * Loads markdown skill files from the skills directory.
 * Skills are included in the system prompt as contextual guidance.
 */

#include <stddef.h>

int skill_loader_init(void);

/* Load all skill files and build a combined text block.
 * Returns MC_OK. buf receives concatenated skill content. */
int skill_loader_get_all(char *buf, size_t size);

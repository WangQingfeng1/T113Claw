#pragma once
/*
 * T113Claw Context Builder
 *
 * Assembles the system prompt from bootstrap files (SOUL, USER),
 * memory context, skills, and tool guidance.
 */

#include <stddef.h>

/* Build system prompt.
 * buf: output buffer (recommend T113CLAW_CONTEXT_BUF_SIZE).
 * Returns MC_OK on success. */
int context_build_system_prompt(char *buf, size_t size);

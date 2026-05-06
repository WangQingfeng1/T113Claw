#pragma once
/*
 * T113Claw Memory Store
 *
 * Persistent memory: SOUL.md (personality), USER.md (user profile),
 * MEMORY.md (long-term facts), daily notes (YYYY-MM-DD.md).
 */

#include <stddef.h>

/* Initialize memory store, ensure directories exist */
int memory_store_init(void);

/* Read SOUL.md into buffer */
int memory_read_soul(char *buf, size_t size);

/* Read USER.md into buffer */
int memory_read_user(char *buf, size_t size);

/* Read long-term memory (MEMORY.md) into buffer */
int memory_read_long_term(char *buf, size_t size);

/* Write/overwrite long-term memory (MEMORY.md) */
int memory_write_long_term(const char *content);

/* Append a note to today's daily memory file (YYYY-MM-DD.md) */
int memory_append_today(const char *note);

/* Read recent daily memories (last N days) into buffer */
int memory_read_recent(char *buf, size_t size, int days);

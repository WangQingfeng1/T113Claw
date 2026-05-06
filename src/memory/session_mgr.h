#pragma once
/*
 * T113Claw Session Manager
 *
 * Per-chat JSONL session files for conversation history.
 */

#include <stddef.h>

/* Initialize session manager */
int session_mgr_init(void);

/* Append a message to a session (JSONL format) */
int session_append(const char *chat_id, const char *role, const char *content);

/* Load session history as a JSON array string for LLM messages.
 * Returns last max_msgs messages as: [{"role":"user","content":"..."},...]
 * Caller must free the returned string. */
char *session_get_history_json(const char *chat_id, int max_msgs);

/* Clear a session (delete the file) */
int session_clear(const char *chat_id);

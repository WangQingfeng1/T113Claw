#pragma once
/*
 * T113Claw LLM Client — Multi-provider abstraction
 *
 * Supports: OpenAI-compatible APIs (GPT, DeepSeek, Qwen, etc.) and Anthropic Claude.
 * Tool use (function calling) is supported via the ReAct pattern.
 */

#include <stddef.h>
#include <stdbool.h>
#include "cJSON.h"
#include "t113claw_config.h"

/* ── Tool call from LLM response ──────────────────────────── */

typedef struct {
    char id[64];        /* tool call id (e.g., "call_xxx" or "toolu_xxx") */
    char name[32];      /* tool name (e.g., "get_current_time") */
    char *input;        /* heap-allocated JSON string of tool input */
    size_t input_len;
} llm_tool_call_t;

/* ── Structured LLM response ─────────────────────────────── */

typedef struct {
    char *text;                                     /* accumulated text content */
    size_t text_len;
    llm_tool_call_t calls[T113CLAW_MAX_TOOL_CALLS];   /* tool calls from response */
    int call_count;
    bool tool_use;                                  /* true if stop_reason is tool_use */
} llm_response_t;

/* Free a response (frees text and tool call inputs) */
void llm_response_free(llm_response_t *resp);

/* ── LLM Client API ──────────────────────────────────────── */

/* Initialize the LLM client */
int llm_client_init(void);

/* Send a chat completion request with optional tools.
 *
 * system_prompt: system message text
 * messages:      cJSON array of {role, content} objects (caller owns)
 * tools_json:    tools array as JSON string, or NULL for no tools
 * resp:          output structured response
 *
 * Returns MC_OK on success.
 */
int llm_chat(const char *system_prompt,
             cJSON *messages,
             const char *tools_json,
             llm_response_t *resp);

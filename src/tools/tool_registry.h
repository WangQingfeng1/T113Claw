#pragma once
/*
 * T113Claw Tool Registry
 *
 * Register tools with JSON schemas, look up and execute by name.
 * Tools are exposed to the LLM for function calling.
 */

#include <stddef.h>

/* Tool definition */
typedef struct {
    const char *name;
    const char *description;
    const char *input_schema_json;  /* JSON Schema string */
    int (*execute)(const char *input_json, char *output, size_t output_size);
} mc_tool_t;

/* Initialize tool registry and register all built-in tools */
int tool_registry_init(void);

/* Get the pre-built tools JSON array string for API requests.
 * Returns NULL if no tools registered. Do not free. */
const char *tool_registry_get_tools_json(void);

/* Execute a tool by name.
 * Returns MC_OK on success, MC_ERR_NOTFOUND if unknown. */
int tool_registry_execute(const char *name, const char *input_json,
                          char *output, size_t output_size);

/*
 * Anthropic Claude LLM Provider
 *
 * Supports: Claude (Messages API with tool_use).
 */

#include "llm_client.h"
#include "config/config.h"
#include "http_client.h"
#include "utils/log.h"
#include "utils/utils.h"
#include "t113claw_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <cJSON.h>

#define TAG "claude"

/* ── Build request body ───────────────────────────────────── */

static char *build_request(const char *system_prompt, cJSON *messages,
                           const char *tools_json)
{
    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "model", config_get_model());
    cJSON_AddNumberToObject(root, "max_tokens", T113CLAW_LLM_MAX_TOKENS);

    /* System prompt (top-level in Claude API) */
    if (system_prompt && system_prompt[0]) {
        cJSON_AddStringToObject(root, "system", system_prompt);
    }

    /* Messages (Claude format: no system role in array) */
    cJSON *msgs = cJSON_CreateArray();
    int count = cJSON_GetArraySize(messages);
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(messages, i);
        cJSON *dup = cJSON_Duplicate(item, 1);
        if (dup) cJSON_AddItemToArray(msgs, dup);
    }
    cJSON_AddItemToObject(root, "messages", msgs);

    /* Tools (Claude native format) */
    if (tools_json && tools_json[0]) {
        cJSON *tools = cJSON_Parse(tools_json);
        if (tools) {
            cJSON_AddItemToObject(root, "tools", tools);
        }
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

/* ── Parse response ───────────────────────────────────────── */

static int parse_response(const char *json_str, llm_response_t *resp)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        LOG_E(TAG, "Failed to parse response JSON");
        return MC_ERR;
    }

    /* Check for error */
    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON *msg = cJSON_GetObjectItem(error, "message");
        LOG_E(TAG, "API error: %s", msg ? msg->valuestring : "unknown");
        cJSON_Delete(root);
        return MC_ERR;
    }

    /* Check stop_reason */
    cJSON *stop_reason = cJSON_GetObjectItem(root, "stop_reason");
    if (stop_reason && stop_reason->valuestring &&
        strcmp(stop_reason->valuestring, "tool_use") == 0) {
        resp->tool_use = true;
    }

    /* Parse content array */
    cJSON *content = cJSON_GetObjectItem(root, "content");
    if (!content || !cJSON_IsArray(content)) {
        cJSON_Delete(root);
        return MC_ERR;
    }

    /* Accumulate text blocks and extract tool_use blocks */
    size_t text_alloc = 0;
    int count = cJSON_GetArraySize(content);
    for (int i = 0; i < count; i++) {
        cJSON *block = cJSON_GetArrayItem(content, i);
        cJSON *type = cJSON_GetObjectItem(block, "type");
        if (!type || !type->valuestring) continue;

        if (strcmp(type->valuestring, "text") == 0) {
            cJSON *text = cJSON_GetObjectItem(block, "text");
            if (text && text->valuestring) {
                size_t len = strlen(text->valuestring);
                char *new_text = realloc(resp->text, text_alloc + len + 1);
                if (new_text) {
                    resp->text = new_text;
                    memcpy(resp->text + text_alloc, text->valuestring, len);
                    text_alloc += len;
                    resp->text[text_alloc] = '\0';
                    resp->text_len = text_alloc;
                }
            }
        } else if (strcmp(type->valuestring, "tool_use") == 0) {
            if (resp->call_count >= T113CLAW_MAX_TOOL_CALLS) continue;

            cJSON *id = cJSON_GetObjectItem(block, "id");
            cJSON *name = cJSON_GetObjectItem(block, "name");
            cJSON *input = cJSON_GetObjectItem(block, "input");

            llm_tool_call_t *tc = &resp->calls[resp->call_count];
            if (id && id->valuestring)
                snprintf(tc->id, 64, "%s", id->valuestring);
            if (name && name->valuestring)
                snprintf(tc->name, 32, "%s", name->valuestring);
            if (input) {
                char *input_str = cJSON_PrintUnformatted(input);
                tc->input = input_str;
                tc->input_len = input_str ? strlen(input_str) : 0;
            }
            resp->call_count++;
            resp->tool_use = true;
        }
    }

    cJSON_Delete(root);
    return MC_OK;
}

/* ── Public API ───────────────────────────────────────────── */

int provider_claude_chat(const char *system_prompt,
                         cJSON *messages,
                         const char *tools_json,
                         llm_response_t *resp)
{
    /* Check API key */
    const char *api_key = config_get_api_key();
    if (!api_key || !api_key[0]) {
        LOG_E(TAG, "API key not configured");
        resp->text = strdup("[Error] API key not configured. Please set api_key in config.ini or t113claw_secrets.h.");
        resp->text_len = resp->text ? strlen(resp->text) : 0;
        return MC_OK;
    }

    const char *url = config_get_api_url();
    if (!url || !url[0]) {
        url = T113CLAW_CLAUDE_API_URL;
    }

    char *body = build_request(system_prompt, messages, tools_json);
    if (!body) return MC_ERR_NOMEM;

    LOG_D(TAG, "Request to %s (%zu bytes)", url, strlen(body));

    /* Claude-specific headers */
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", api_key);

    char version_header[64];
    snprintf(version_header, sizeof(version_header),
             "anthropic-version: %s", T113CLAW_CLAUDE_API_VERSION);

    const char *headers[] = {
        auth_header,
        version_header,
        "Content-Type: application/json",
        NULL
    };

    http_response_t http_resp;
    int rc = http_post_json(url, body, headers, &http_resp);
    free(body);

    if (rc != MC_OK) {
        LOG_E(TAG, "HTTP request failed");
        return rc;
    }

    if (http_resp.http_code != 200) {
        LOG_E(TAG, "API returned HTTP %ld: %.200s",
               http_resp.http_code, http_resp.data ? http_resp.data : "");
        http_response_free(&http_resp);
        return MC_ERR;
    }

    rc = parse_response(http_resp.data, resp);
    http_response_free(&http_resp);

    return rc;
}

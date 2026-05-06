/*
 * OpenAI-compatible LLM Provider
 *
 * Supports: OpenAI, DeepSeek, Qwen, and any OpenAI-compatible endpoint.
 * Handles tool_calls (function calling) in the response.
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

#define TAG "openai"

/* ── Build request body ───────────────────────────────────── */

static char *build_request(const char *system_prompt, cJSON *messages,
                           const char *tools_json)
{
    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "model", config_get_model());
    cJSON_AddNumberToObject(root, "max_tokens", T113CLAW_LLM_MAX_TOKENS);

    /* Messages array: prepend system message */
    cJSON *msgs = cJSON_CreateArray();

    if (system_prompt && system_prompt[0]) {
        cJSON *sys = cJSON_CreateObject();
        cJSON_AddStringToObject(sys, "role", "system");
        cJSON_AddStringToObject(sys, "content", system_prompt);
        cJSON_AddItemToArray(msgs, sys);
    }

    /* Append conversation history (deep copy to avoid reference issues) */
    int count = cJSON_GetArraySize(messages);
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(messages, i);
        cJSON *dup = cJSON_Duplicate(item, 1);
        if (dup) cJSON_AddItemToArray(msgs, dup);
    }

    cJSON_AddItemToObject(root, "messages", msgs);

    /* Tools (function calling) */
    if (tools_json && tools_json[0]) {
        cJSON *tools_arr = cJSON_Parse(tools_json);
        if (tools_arr) {
            /* Wrap each tool: { "type": "function", "function": { ... } } */
            cJSON *wrapped = cJSON_CreateArray();
            int tc = cJSON_GetArraySize(tools_arr);
            for (int i = 0; i < tc; i++) {
                cJSON *t = cJSON_GetArrayItem(tools_arr, i);
                cJSON *wrapper = cJSON_CreateObject();
                cJSON_AddStringToObject(wrapper, "type", "function");

                cJSON *func = cJSON_CreateObject();
                cJSON *name = cJSON_GetObjectItem(t, "name");
                cJSON *desc = cJSON_GetObjectItem(t, "description");
                cJSON *schema = cJSON_GetObjectItem(t, "input_schema");

                if (name) cJSON_AddStringToObject(func, "name", name->valuestring);
                if (desc) cJSON_AddStringToObject(func, "description", desc->valuestring);
                if (schema) {
                    cJSON *schema_dup = cJSON_Duplicate(schema, 1);
                    if (schema_dup) cJSON_AddItemToObject(func, "parameters", schema_dup);
                }

                cJSON_AddItemToObject(wrapper, "function", func);
                cJSON_AddItemToArray(wrapped, wrapper);
            }
            cJSON_AddItemToObject(root, "tools", wrapped);
            cJSON_Delete(tools_arr);
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

    /* Extract first choice */
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (!choices || cJSON_GetArraySize(choices) == 0) {
        LOG_E(TAG, "No choices in response");
        cJSON_Delete(root);
        return MC_ERR;
    }

    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(choice, "message");
    cJSON *finish_reason = cJSON_GetObjectItem(choice, "finish_reason");

    /* Text content */
    cJSON *content = cJSON_GetObjectItem(message, "content");
    if (content && content->valuestring) {
        resp->text = strdup(content->valuestring);
        resp->text_len = strlen(resp->text);
    }

    /* Check finish_reason */
    if (finish_reason && finish_reason->valuestring &&
        strcmp(finish_reason->valuestring, "tool_calls") == 0) {
        resp->tool_use = true;
    }

    /* Tool calls */
    cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
    if (tool_calls && cJSON_IsArray(tool_calls)) {
        resp->tool_use = true;
        int tc = cJSON_GetArraySize(tool_calls);
        if (tc > T113CLAW_MAX_TOOL_CALLS) tc = T113CLAW_MAX_TOOL_CALLS;

        for (int i = 0; i < tc; i++) {
            cJSON *tc_item = cJSON_GetArrayItem(tool_calls, i);
            cJSON *id = cJSON_GetObjectItem(tc_item, "id");
            cJSON *func = cJSON_GetObjectItem(tc_item, "function");
            if (!func) continue;

            cJSON *name = cJSON_GetObjectItem(func, "name");
            cJSON *args = cJSON_GetObjectItem(func, "arguments");

            if (id && id->valuestring)
                snprintf(resp->calls[resp->call_count].id, 64, "%s", id->valuestring);
            if (name && name->valuestring)
                snprintf(resp->calls[resp->call_count].name, 32, "%s", name->valuestring);
            if (args && args->valuestring) {
                resp->calls[resp->call_count].input = strdup(args->valuestring);
                resp->calls[resp->call_count].input_len = strlen(args->valuestring);
            }
            resp->call_count++;
        }
    }

    cJSON_Delete(root);
    return MC_OK;
}

/* ── Public API ───────────────────────────────────────────── */

int provider_openai_chat(const char *system_prompt,
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
        return MC_OK;  /* Return as normal text so user sees the message */
    }

    /* Determine API URL */
    const char *url = config_get_api_url();
    if (!url || !url[0]) {
        url = T113CLAW_OPENAI_API_URL;
    }

    /* Build request */
    char *body = build_request(system_prompt, messages, tools_json);
    if (!body) return MC_ERR_NOMEM;

    LOG_D(TAG, "Request to %s (%zu bytes)", url, strlen(body));

    /* Headers */
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);

    const char *headers[] = {
        auth_header,
        "Content-Type: application/json",
        NULL
    };

    /* Send request */
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

    /* Parse response */
    rc = parse_response(http_resp.data, resp);
    http_response_free(&http_resp);

    return rc;
}

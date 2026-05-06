#include "agent_loop.h"
#include "context_builder.h"
#include "bus/message_bus.h"
#include "llm/llm_client.h"
#include "tools/tool_registry.h"
#include "memory/session_mgr.h"
#include "config/config.h"
#include "t113claw_config.h"
#include "ui/ui_manager.h"
#include "utils/log.h"
#include "utils/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <cJSON.h>

#define TAG "agent"

static pthread_t s_thread;
static volatile bool s_running = false;

/* ── Provider-aware tool call message building ────────────── */

static bool is_claude_provider(void)
{
    const char *p = config_get_provider();
    return (strcmp(p, "claude") == 0 || strcmp(p, "anthropic") == 0);
}

/* Append assistant tool-call message + tool result messages (OpenAI format) */
static void append_tool_messages_openai(cJSON *messages,
                                        const llm_response_t *resp,
                                        const char *tool_outputs[],
                                        int count)
{
    /* Assistant message with tool_calls array */
    cJSON *asst = cJSON_CreateObject();
    cJSON_AddStringToObject(asst, "role", "assistant");
    if (resp->text && resp->text[0]) {
        cJSON_AddStringToObject(asst, "content", resp->text);
    } else {
        cJSON_AddNullToObject(asst, "content");
    }

    cJSON *tc_arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *tc = cJSON_CreateObject();
        cJSON_AddStringToObject(tc, "id", resp->calls[i].id);
        cJSON_AddStringToObject(tc, "type", "function");
        cJSON *func = cJSON_CreateObject();
        cJSON_AddStringToObject(func, "name", resp->calls[i].name);
        cJSON_AddStringToObject(func, "arguments",
                                resp->calls[i].input ? resp->calls[i].input : "{}");
        cJSON_AddItemToObject(tc, "function", func);
        cJSON_AddItemToArray(tc_arr, tc);
    }
    cJSON_AddItemToObject(asst, "tool_calls", tc_arr);
    cJSON_AddItemToArray(messages, asst);

    /* Tool result messages */
    for (int i = 0; i < count; i++) {
        cJSON *tool_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(tool_msg, "role", "tool");
        cJSON_AddStringToObject(tool_msg, "tool_call_id", resp->calls[i].id);
        cJSON_AddStringToObject(tool_msg, "content", tool_outputs[i]);
        cJSON_AddItemToArray(messages, tool_msg);
    }
}

/* Append assistant tool-call message + tool result messages (Claude format) */
static void append_tool_messages_claude(cJSON *messages,
                                        const llm_response_t *resp,
                                        const char *tool_outputs[],
                                        int count)
{
    /* Assistant message: content is array of text + tool_use blocks */
    cJSON *asst = cJSON_CreateObject();
    cJSON_AddStringToObject(asst, "role", "assistant");

    cJSON *content_arr = cJSON_CreateArray();
    if (resp->text && resp->text[0]) {
        cJSON *text_block = cJSON_CreateObject();
        cJSON_AddStringToObject(text_block, "type", "text");
        cJSON_AddStringToObject(text_block, "text", resp->text);
        cJSON_AddItemToArray(content_arr, text_block);
    }
    for (int i = 0; i < count; i++) {
        cJSON *tu = cJSON_CreateObject();
        cJSON_AddStringToObject(tu, "type", "tool_use");
        cJSON_AddStringToObject(tu, "id", resp->calls[i].id);
        cJSON_AddStringToObject(tu, "name", resp->calls[i].name);
        cJSON *input = cJSON_Parse(resp->calls[i].input ? resp->calls[i].input : "{}");
        cJSON_AddItemToObject(tu, "input", input ? input : cJSON_CreateObject());
        cJSON_AddItemToArray(content_arr, tu);
    }
    cJSON_AddItemToObject(asst, "content", content_arr);
    cJSON_AddItemToArray(messages, asst);

    /* Tool results: single user message with content array of tool_result blocks */
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON *result_arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *tr = cJSON_CreateObject();
        cJSON_AddStringToObject(tr, "type", "tool_result");
        cJSON_AddStringToObject(tr, "tool_use_id", resp->calls[i].id);
        cJSON_AddStringToObject(tr, "content", tool_outputs[i]);
        cJSON_AddItemToArray(result_arr, tr);
    }
    cJSON_AddItemToObject(user_msg, "content", result_arr);
    cJSON_AddItemToArray(messages, user_msg);
}

/* ── Process one message through the ReAct loop ─────────── */

static void process_message(const mc_msg_t *msg)
{
    LOG_I(TAG, "Processing [%s/%s]: %.80s",
           msg->channel, msg->chat_id, msg->content);

    ui_manager_notify_user_message(msg->channel, msg->content);
    ui_manager_notify_agent_state("THINKING");

    /* 1. Build system prompt */
    char *sys_prompt = malloc(T113CLAW_CONTEXT_BUF_SIZE);
    if (!sys_prompt) {
        LOG_E(TAG, "Out of memory for system prompt");
        return;
    }
    context_build_system_prompt(sys_prompt, T113CLAW_CONTEXT_BUF_SIZE);

    /* 2. Load session history */
    char *history_json = session_get_history_json(msg->chat_id,
                                                   T113CLAW_AGENT_MAX_HISTORY);

    /* 3. Build messages array */
    cJSON *messages = cJSON_Parse(history_json ? history_json : "[]");
    free(history_json);

    /* Append current user message */
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", msg->content);
    cJSON_AddItemToArray(messages, user_msg);

    /* Save user message to session */
    session_append(msg->chat_id, "user", msg->content);

    /* 4. Get tools JSON */
    const char *tools_json = tool_registry_get_tools_json();

    /* 5. ReAct loop */
    char *final_text = NULL;

    for (int iter = 0; iter < T113CLAW_AGENT_MAX_TOOL_ITER; iter++) {
        llm_response_t resp;
        int rc = llm_chat(sys_prompt, messages, tools_json, &resp);

        if (rc != MC_OK) {
            LOG_E(TAG, "LLM call failed");
            final_text = strdup("Sorry, I encountered an error calling the LLM.");
            llm_response_free(&resp);
            break;
        }

        if (resp.tool_use && resp.call_count > 0) {
            ui_manager_notify_agent_state("TOOLS");
            /* Execute each tool and collect outputs */
            const char *tool_outputs[T113CLAW_MAX_TOOL_CALLS];
            char tool_bufs[T113CLAW_MAX_TOOL_CALLS][T113CLAW_TOOL_OUTPUT_SIZE];

            for (int i = 0; i < resp.call_count; i++) {
                tool_registry_execute(resp.calls[i].name,
                                      resp.calls[i].input ? resp.calls[i].input : "{}",
                                      tool_bufs[i], sizeof(tool_bufs[i]));
                tool_outputs[i] = tool_bufs[i];
                LOG_I(TAG, "Tool '%s' result: %.120s", resp.calls[i].name, tool_bufs[i]);
            }

            /* Append tool messages in provider-appropriate format */
            if (is_claude_provider()) {
                append_tool_messages_claude(messages, &resp, tool_outputs, resp.call_count);
            } else {
                append_tool_messages_openai(messages, &resp, tool_outputs, resp.call_count);
            }

            llm_response_free(&resp);
            /* Continue loop — LLM will see tool results */
            continue;
        }

        /* No tool use — we have a final response */
        if (resp.text && resp.text[0]) {
            final_text = strdup(resp.text);
        } else {
            final_text = strdup("(no response)");
        }
        llm_response_free(&resp);
        break;
    }

    if (!final_text) {
        final_text = strdup("Max tool iterations reached.");
    }

    /* 6. Save assistant response to session */
    session_append(msg->chat_id, "assistant", final_text);

    /* 7. Push response to outbound queue */
    mc_msg_t out = {0};
    snprintf(out.channel, sizeof(out.channel), "%s", msg->channel);
    snprintf(out.chat_id, sizeof(out.chat_id), "%s", msg->chat_id);
    out.content = final_text;  /* bus takes ownership */
    message_bus_push_outbound(&out);

    ui_manager_notify_agent_state("READY");

    /* 8. Cleanup */
    free(sys_prompt);
    cJSON_Delete(messages);
}

/* ── Agent thread ─────────────────────────────────────────── */

static void *agent_thread(void *arg)
{
    (void)arg;
    LOG_I(TAG, "Agent loop started");

    while (s_running) {
        mc_msg_t msg;
        int rc = message_bus_pop_inbound(&msg, 1000);
        if (rc == MC_ERR_TIMEOUT) continue;
        if (rc != MC_OK) continue;

        process_message(&msg);
        free(msg.content);
    }

    LOG_I(TAG, "Agent loop stopped");
    return NULL;
}

/* ── Public API ───────────────────────────────────────────── */

int agent_loop_init(void)
{
    LOG_I(TAG, "Agent loop initialized");
    return MC_OK;
}

int agent_loop_start(void)
{
    s_running = true;
    int rc = pthread_create(&s_thread, NULL, agent_thread, NULL);
    if (rc != 0) {
        LOG_E(TAG, "Failed to create agent thread");
        return MC_ERR;
    }

    /* Set thread name for debugging */
    pthread_setname_np(s_thread, "agent_loop");
    return MC_OK;
}

void agent_loop_stop(void)
{
    s_running = false;
    pthread_join(s_thread, NULL);
}

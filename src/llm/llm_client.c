#include "llm_client.h"
#include "config/config.h"
#include "utils/log.h"
#include "utils/utils.h"

#include <string.h>

#define TAG "llm"

/* Provider dispatch functions (defined in provider_*.c) */
extern int provider_openai_chat(const char *system_prompt,
                                cJSON *messages,
                                const char *tools_json,
                                llm_response_t *resp);

extern int provider_claude_chat(const char *system_prompt,
                                cJSON *messages,
                                const char *tools_json,
                                llm_response_t *resp);

/* ── Public API ───────────────────────────────────────────── */

int llm_client_init(void)
{
    LOG_I(TAG, "LLM client initialized (provider: %s, model: %s)",
           config_get_provider(), config_get_model());
    return MC_OK;
}

void llm_response_free(llm_response_t *resp)
{
    if (!resp) return;
    free(resp->text);
    resp->text = NULL;
    resp->text_len = 0;
    for (int i = 0; i < resp->call_count; i++) {
        free(resp->calls[i].input);
        resp->calls[i].input = NULL;
    }
    resp->call_count = 0;
    resp->tool_use = false;
}

int llm_chat(const char *system_prompt,
             cJSON *messages,
             const char *tools_json,
             llm_response_t *resp)
{
    const char *provider = config_get_provider();

    memset(resp, 0, sizeof(*resp));

    if (strcmp(provider, "claude") == 0 || strcmp(provider, "anthropic") == 0) {
        return provider_claude_chat(system_prompt, messages, tools_json, resp);
    } else {
        /* Default: OpenAI-compatible (covers openai, deepseek, qwen, etc.) */
        return provider_openai_chat(system_prompt, messages, tools_json, resp);
    }
}

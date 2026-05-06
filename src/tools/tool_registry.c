#include "tool_registry.h"
#include "utils/log.h"
#include "utils/utils.h"

#include <string.h>
#include <stdlib.h>
#include <cJSON.h>

#define TAG "tools"
#define MAX_TOOLS 16

/* External tool init/execute declarations */
extern int tool_time_execute(const char *input, char *output, size_t size);
extern int tool_file_read_execute(const char *input, char *output, size_t size);
extern int tool_file_write_execute(const char *input, char *output, size_t size);
extern int tool_file_list_execute(const char *input, char *output, size_t size);
extern int tool_system_execute(const char *input, char *output, size_t size);
extern int tool_cron_add_execute(const char *input, char *output, size_t size);
extern int tool_cron_list_execute(const char *input, char *output, size_t size);
extern int tool_cron_remove_execute(const char *input, char *output, size_t size);
extern int tool_exec_execute(const char *input, char *output, size_t size);
extern int tool_remote_exec_execute(const char *input, char *output, size_t size);
extern int tool_web_search_execute(const char *input, char *output, size_t size);

static mc_tool_t s_tools[MAX_TOOLS];
static int s_tool_count = 0;
static char *s_tools_json = NULL;

/* ── Internal ─────────────────────────────────────────────── */

static void register_tool(const mc_tool_t *tool)
{
    if (s_tool_count >= MAX_TOOLS) {
        LOG_E(TAG, "Tool registry full");
        return;
    }
    s_tools[s_tool_count++] = *tool;
    LOG_D(TAG, "Registered tool: %s", tool->name);
}

static void build_tools_json(void)
{
    cJSON *arr = cJSON_CreateArray();

    for (int i = 0; i < s_tool_count; i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", s_tools[i].name);
        cJSON_AddStringToObject(tool, "description", s_tools[i].description);

        cJSON *schema = cJSON_Parse(s_tools[i].input_schema_json);
        if (schema) {
            cJSON_AddItemToObject(tool, "input_schema", schema);
        }

        cJSON_AddItemToArray(arr, tool);
    }

    free(s_tools_json);
    s_tools_json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
}

/* ── Public API ───────────────────────────────────────────── */

int tool_registry_init(void)
{
    s_tool_count = 0;

    /* get_current_time */
    register_tool(&(mc_tool_t){
        .name = "get_current_time",
        .description = "Get the current date, time and timezone.",
        .input_schema_json = "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
        .execute = tool_time_execute,
    });

    /* read_file */
    register_tool(&(mc_tool_t){
        .name = "read_file",
        .description = "Read a file from the data directory.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"File path relative to data dir\"}},"
            "\"required\":[\"path\"]}",
        .execute = tool_file_read_execute,
    });

    /* write_file */
    register_tool(&(mc_tool_t){
        .name = "write_file",
        .description = "Write or overwrite a file in the data directory.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"path\":{\"type\":\"string\",\"description\":\"File path relative to data dir\"},"
            "\"content\":{\"type\":\"string\",\"description\":\"Content to write\"}},"
            "\"required\":[\"path\",\"content\"]}",
        .execute = tool_file_write_execute,
    });

    /* list_dir */
    register_tool(&(mc_tool_t){
        .name = "list_dir",
        .description = "List files in the data directory.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"prefix\":{\"type\":\"string\",\"description\":\"Optional subdirectory to list\"}},"
            "\"required\":[]}",
        .execute = tool_file_list_execute,
    });

    /* system_info */
    register_tool(&(mc_tool_t){
        .name = "system_info",
        .description = "Get system information: CPU temperature, memory usage, uptime, storage.",
        .input_schema_json = "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
        .execute = tool_system_execute,
    });

    /* cron_add */
    register_tool(&(mc_tool_t){
        .name = "cron_add",
        .description = "Schedule a recurring or one-shot task. The message triggers an agent turn when fired.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"name\":{\"type\":\"string\",\"description\":\"Short job name\"},"
            "\"schedule_type\":{\"type\":\"string\",\"description\":\"'every' for recurring or 'at' for one-shot\"},"
            "\"interval_s\":{\"type\":\"integer\",\"description\":\"Interval seconds (for 'every')\"},"
            "\"at_epoch\":{\"type\":\"integer\",\"description\":\"Unix timestamp (for 'at')\"},"
            "\"message\":{\"type\":\"string\",\"description\":\"Message to inject when job fires\"},"
            "\"channel\":{\"type\":\"string\",\"description\":\"Reply channel (optional)\"},"
            "\"chat_id\":{\"type\":\"string\",\"description\":\"Reply chat_id (optional)\"}"
            "},\"required\":[\"name\",\"schedule_type\",\"message\"]}",
        .execute = tool_cron_add_execute,
    });

    /* cron_list */
    register_tool(&(mc_tool_t){
        .name = "cron_list",
        .description = "List all scheduled cron jobs.",
        .input_schema_json = "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
        .execute = tool_cron_list_execute,
    });

    /* cron_remove */
    register_tool(&(mc_tool_t){
        .name = "cron_remove",
        .description = "Remove a scheduled cron job by its ID.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"job_id\":{\"type\":\"string\",\"description\":\"Job ID to remove\"}},"
            "\"required\":[\"job_id\"]}",
        .execute = tool_cron_remove_execute,
    });

    /* run_command */
    register_tool(&(mc_tool_t){
        .name = "run_command",
        .description = "Execute a shell command on the device and return stdout+stderr. "
                       "Use this to: run programs, compile code, read hardware (GPIO/sensors), "
                       "manage files/services, install packages, or any system operation. "
                       "You can write code to a file with write_file, then compile and run it with this tool. "
                       "Default timeout is 30 seconds, max 120 seconds.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"command\":{\"type\":\"string\",\"description\":\"Shell command to execute\"},"
            "\"timeout\":{\"type\":\"integer\",\"description\":\"Timeout in seconds (default 30, max 120)\"},"
            "\"working_directory\":{\"type\":\"string\",\"description\":\"Working directory for the command (optional)\"}"
            "},\"required\":[\"command\"]}",
        .execute = tool_exec_execute,
    });

    /* web_search */
    register_tool(&(mc_tool_t){
        .name = "web_search",
        .description = "Search the web for up-to-date external information. "
                       "Use this when the question depends on recent events, external documentation, "
                       "product information, troubleshooting posts, or knowledge not stored locally. "
                       "In auto mode it prefers Tavily when configured and falls back to Sogou when needed.",
        .input_schema_json =
            "{\"type\":\"object\"," 
            "\"properties\":{"
            "\"query\":{\"type\":\"string\",\"description\":\"What to search for on the web\"},"
            "\"site\":{\"type\":\"string\",\"description\":\"Optional domain restriction, e.g. docs.lvgl.io\"},"
            "\"max_results\":{\"type\":\"integer\",\"description\":\"Optional result count, default 5, max 8\"}"
            "},\"required\":[\"query\"]}",
        .execute = tool_web_search_execute,
    });

    /* remote_exec */
    register_tool(&(mc_tool_t){
        .name = "remote_exec",
        .description = "Run a shell command on the configured LAN Linux server via the T113Claw remote agent. "
                       "Use this when the user wants to control their PC/server from the device. "
                       "Return stdout, stderr and exit code.",
        .input_schema_json =
            "{\"type\":\"object\"," 
            "\"properties\":{"
            "\"command\":{\"type\":\"string\",\"description\":\"Shell command to execute on the configured remote server\"},"
            "\"timeout\":{\"type\":\"integer\",\"description\":\"Timeout in seconds (default 30, max 120)\"},"
            "\"working_directory\":{\"type\":\"string\",\"description\":\"Optional working directory on the remote server\"}"
            "},\"required\":[\"command\"]}",
        .execute = tool_remote_exec_execute,
    });

    build_tools_json();

    LOG_I(TAG, "Tool registry initialized (%d tools)", s_tool_count);
    return MC_OK;
}

const char *tool_registry_get_tools_json(void)
{
    return s_tools_json;
}

int tool_registry_execute(const char *name, const char *input_json,
                          char *output, size_t output_size)
{
    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            LOG_I(TAG, "Executing tool: %s", name);
            return s_tools[i].execute(input_json, output, output_size);
        }
    }

    LOG_W(TAG, "Unknown tool: %s", name);
    snprintf(output, output_size, "Error: unknown tool '%s'", name);
    return MC_ERR_NOTFOUND;
}

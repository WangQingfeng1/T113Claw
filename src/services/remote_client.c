#include "services/remote_client.h"

#include "config/config.h"
#include "http_client.h"
#include "t113claw_config.h"
#include "utils/log.h"
#include "utils/utils.h"

#include <cJSON.h>
#include <openssl/evp.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define TAG "remote"

typedef struct {
    char host[REMOTE_CLIENT_TARGET_LEN];
    char port[32];
    char username[128];
    char password[128];
    char target[REMOTE_CLIENT_TARGET_LEN];
} remote_config_t;

static void remote_error(char *error, size_t error_size, const char *fmt, ...)
{
    va_list ap;

    if (!error || error_size == 0) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(error, error_size, fmt, ap);
    va_end(ap);
}

static const char *json_string(cJSON *obj, const char *key)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsString(item) && item->valuestring) {
        return item->valuestring;
    }
    return "";
}

static int json_int(cJSON *obj, const char *key, int def)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return def;
}

static int load_remote_config(remote_config_t *cfg, char *error, size_t error_size)
{
    const char *host = config_get_default("remote", "host", "");
    const char *port = config_get_default("remote", "port", T113CLAW_REMOTE_PORT_DEFAULT);
    const char *username = config_get_default("remote", "username", "");
    const char *password = config_get_default("remote", "password", "");

    memset(cfg, 0, sizeof(*cfg));

    if (!host[0] || !username[0] || !password[0]) {
        remote_error(error, error_size,
                     "remote server is not configured. Set [remote] host, username and password.");
        return MC_ERR;
    }

    snprintf(cfg->host, sizeof(cfg->host), "%s", host);
    snprintf(cfg->port, sizeof(cfg->port), "%s", port && port[0] ? port : T113CLAW_REMOTE_PORT_DEFAULT);
    snprintf(cfg->username, sizeof(cfg->username), "%s", username);
    snprintf(cfg->password, sizeof(cfg->password), "%s", password);

    if (strncmp(cfg->host, "http://", 7) == 0 || strncmp(cfg->host, "https://", 8) == 0) {
        snprintf(cfg->target, sizeof(cfg->target), "%s", cfg->host);
    } else {
        snprintf(cfg->target, sizeof(cfg->target), "%.220s:%.31s", cfg->host, cfg->port);
    }

    return MC_OK;
}

static int build_basic_auth_header(const remote_config_t *cfg,
                                   char *header, size_t header_size)
{
    char plain[256];
    char encoded[384];
    int plain_len;
    int encoded_len;

    plain_len = snprintf(plain, sizeof(plain), "%s:%s", cfg->username, cfg->password);
    if (plain_len <= 0 || (size_t)plain_len >= sizeof(plain)) {
        return MC_ERR;
    }

    encoded_len = EVP_EncodeBlock((unsigned char *)encoded,
                                  (const unsigned char *)plain, plain_len);
    if (encoded_len <= 0) {
        return MC_ERR;
    }

    if (snprintf(header, header_size, "Authorization: Basic %s", encoded) >=
        (int)header_size) {
        return MC_ERR;
    }

    return MC_OK;
}

static int build_url(const remote_config_t *cfg, const char *path,
                     char *url, size_t url_size)
{
    if (strncmp(cfg->host, "http://", 7) == 0 || strncmp(cfg->host, "https://", 8) == 0) {
        size_t host_len = strlen(cfg->host);
        const char *sep = (host_len > 0 && cfg->host[host_len - 1] == '/') ? "" : "/";
        const char *path_part = (path[0] == '/') ? path + 1 : path;

        if (snprintf(url, url_size, "%s%s%s", cfg->host, sep, path_part) >= (int)url_size) {
            return MC_ERR;
        }
        return MC_OK;
    }

    if (snprintf(url, url_size, "http://%s:%s%s", cfg->host, cfg->port, path) >= (int)url_size) {
        return MC_ERR;
    }
    return MC_OK;
}

static int handle_http_error(const char *url, const http_response_t *resp,
                             char *error, size_t error_size)
{
    if (resp->http_code != 200) {
        remote_error(error, error_size,
                     "remote agent returned HTTP %ld%s%s",
                     resp->http_code,
                     (resp->data && resp->data[0]) ? " - " : "",
                     (resp->data && resp->data[0]) ? resp->data : "");
        return MC_ERR;
    }

    if (!resp->data || !resp->data[0]) {
        remote_error(error, error_size, "remote agent returned an empty response from %s", url);
        return MC_ERR;
    }

    return MC_OK;
}

int remote_client_status(remote_status_t *status, char *error, size_t error_size)
{
    remote_config_t cfg;
    char url[512];
    char auth_header[512];
    const char *headers[3];
    http_response_t resp;
    cJSON *root = NULL;

    if (!status) {
        remote_error(error, error_size, "invalid status buffer");
        return MC_ERR;
    }

    if (load_remote_config(&cfg, error, error_size) != MC_OK) {
        return MC_ERR;
    }

    if (build_url(&cfg, T113CLAW_REMOTE_STATUS_PATH, url, sizeof(url)) != MC_OK) {
        remote_error(error, error_size, "remote status URL is too long");
        return MC_ERR;
    }

    if (build_basic_auth_header(&cfg, auth_header, sizeof(auth_header)) != MC_OK) {
        remote_error(error, error_size, "failed to build Authorization header");
        return MC_ERR;
    }

    headers[0] = auth_header;
    headers[1] = "Accept: application/json";
    headers[2] = NULL;

    LOG_I(TAG, "Remote status probe: target=%.160s", cfg.target);
    if (http_get(url, headers, &resp) != MC_OK) {
        remote_error(error, error_size, "failed to reach remote agent at %s", url);
        return MC_ERR;
    }

    if (handle_http_error(url, &resp, error, error_size) != MC_OK) {
        http_response_free(&resp);
        return MC_ERR;
    }

    root = cJSON_Parse(resp.data);
    if (!root) {
        remote_error(error, error_size, "remote agent returned invalid JSON: %s", resp.data);
        http_response_free(&resp);
        return MC_ERR;
    }

    memset(status, 0, sizeof(*status));
    snprintf(status->target, sizeof(status->target), "%s", cfg.target);
    snprintf(status->hostname, sizeof(status->hostname), "%s", json_string(root, "hostname"));
    snprintf(status->platform, sizeof(status->platform), "%s", json_string(root, "platform"));
    snprintf(status->cwd, sizeof(status->cwd), "%s", json_string(root, "cwd"));
    snprintf(status->user, sizeof(status->user), "%s", json_string(root, "user"));

    cJSON_Delete(root);
    http_response_free(&resp);
    return MC_OK;
}

int remote_client_exec(const char *command,
                       const char *working_directory,
                       int timeout_s,
                       remote_exec_result_t *result,
                       char *error,
                       size_t error_size)
{
    remote_config_t cfg;
    char url[512];
    char auth_header[512];
    const char *headers[3];
    cJSON *body = NULL;
    char *body_json = NULL;
    http_response_t resp;
    cJSON *root = NULL;

    if (!command || !command[0] || !result) {
        remote_error(error, error_size, "invalid remote exec request");
        return MC_ERR;
    }

    if (timeout_s <= 0) {
        timeout_s = T113CLAW_REMOTE_DEFAULT_TIMEOUT_S;
    }
    if (timeout_s > T113CLAW_REMOTE_MAX_TIMEOUT_S) {
        timeout_s = T113CLAW_REMOTE_MAX_TIMEOUT_S;
    }

    if (load_remote_config(&cfg, error, error_size) != MC_OK) {
        return MC_ERR;
    }

    if (build_url(&cfg, T113CLAW_REMOTE_EXEC_PATH, url, sizeof(url)) != MC_OK) {
        remote_error(error, error_size, "remote exec URL is too long");
        return MC_ERR;
    }

    if (build_basic_auth_header(&cfg, auth_header, sizeof(auth_header)) != MC_OK) {
        remote_error(error, error_size, "failed to build Authorization header");
        return MC_ERR;
    }

    body = cJSON_CreateObject();
    if (!body) {
        remote_error(error, error_size, "out of memory while building request");
        return MC_ERR;
    }
    cJSON_AddStringToObject(body, "command", command);
    cJSON_AddNumberToObject(body, "timeout", timeout_s);
    if (working_directory && working_directory[0]) {
        cJSON_AddStringToObject(body, "working_directory", working_directory);
    }

    body_json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_json) {
        remote_error(error, error_size, "failed to serialize request body");
        return MC_ERR;
    }

    headers[0] = auth_header;
    headers[1] = "Accept: application/json";
    headers[2] = NULL;

    LOG_I(TAG, "Remote exec: target=%.96s command=%.48s timeout=%d",
          cfg.target, command, timeout_s);
    if (http_post_json(url, body_json, headers, &resp) != MC_OK) {
        free(body_json);
        remote_error(error, error_size, "failed to reach remote agent at %s", url);
        return MC_ERR;
    }
    free(body_json);

    if (handle_http_error(url, &resp, error, error_size) != MC_OK) {
        http_response_free(&resp);
        return MC_ERR;
    }

    root = cJSON_Parse(resp.data);
    if (!root) {
        remote_error(error, error_size, "remote agent returned invalid JSON: %s", resp.data);
        http_response_free(&resp);
        return MC_ERR;
    }

    memset(result, 0, sizeof(*result));
    snprintf(result->target, sizeof(result->target), "%s", cfg.target);
    snprintf(result->command, sizeof(result->command), "%s", command);
    result->exit_code = json_int(root, "exit_code", -1);
    result->duration_ms = json_int(root, "duration_ms", -1);
    result->timed_out = cJSON_IsTrue(cJSON_GetObjectItem(root, "timed_out"));
    result->stdout_truncated = cJSON_IsTrue(cJSON_GetObjectItem(root, "stdout_truncated"));
    result->stderr_truncated = cJSON_IsTrue(cJSON_GetObjectItem(root, "stderr_truncated"));
    snprintf(result->stdout_text, sizeof(result->stdout_text), "%s", json_string(root, "stdout"));
    snprintf(result->stderr_text, sizeof(result->stderr_text), "%s", json_string(root, "stderr"));

    cJSON_Delete(root);
    http_response_free(&resp);
    return MC_OK;
}
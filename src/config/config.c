#include "config.h"
#include "t113claw_config.h"
#include "utils/log.h"
#include "utils/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>

#define TAG "config"

#define MAX_ENTRIES 64
#define MAX_KEY_LEN 64
#define MAX_VAL_LEN 256
#define MAX_SEC_LEN 32

typedef struct {
    char section[MAX_SEC_LEN];
    char key[MAX_KEY_LEN];
    char value[MAX_VAL_LEN];
} config_entry_t;

static config_entry_t s_entries[MAX_ENTRIES];
static int s_count = 0;
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Internal helpers ─────────────────────────────────────── */

static config_entry_t *find_entry(const char *section, const char *key)
{
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_entries[i].section, section) == 0 &&
            strcmp(s_entries[i].key, key) == 0) {
            return &s_entries[i];
        }
    }
    return NULL;
}

static int add_entry(const char *section, const char *key, const char *value)
{
    config_entry_t *e = find_entry(section, key);
    if (e) {
        snprintf(e->value, MAX_VAL_LEN, "%s", value);
        return 0;
    }
    if (s_count >= MAX_ENTRIES) return -1;

    e = &s_entries[s_count++];
    snprintf(e->section, MAX_SEC_LEN, "%s", section);
    snprintf(e->key, MAX_KEY_LEN, "%s", key);
    snprintf(e->value, MAX_VAL_LEN, "%s", value);
    return 0;
}

static void trim(char *s)
{
    /* Remove leading whitespace via memmove */
    char *start = s;
    while (*start == ' ' || *start == '\t') start++;
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
    /* Remove trailing whitespace */
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end-- = '\0';
    }
}

/* ── Parse INI file ───────────────────────────────────────── */

static int parse_ini(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        LOG_W(TAG, "Config file not found: %s (using defaults)", path);
        return -1;
    }

    char line[512];
    char current_section[MAX_SEC_LEN] = "general";

    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';')
            continue;

        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end) {
                *end = '\0';
                snprintf(current_section, MAX_SEC_LEN, "%s", line + 1);
            }
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim(key);
        trim(val);

        if (key[0] && val[0]) {
            add_entry(current_section, key, val);
        }
    }

    fclose(f);
    return 0;
}

/* ── Load build-time defaults ─────────────────────────────── */

static void load_defaults(void)
{
    /* Only set if build-time secret is non-empty */
    #define SET_DEFAULT(sec, key, val) \
        if ((val)[0] != '\0') add_entry(sec, key, val)

    SET_DEFAULT("general", "device_name", "T113Claw-T113");
    SET_DEFAULT("general", "log_level",   "1");

    SET_DEFAULT("llm", "provider",  T113CLAW_SECRET_MODEL_PROVIDER);
    SET_DEFAULT("llm", "api_key",   T113CLAW_SECRET_API_KEY);
    SET_DEFAULT("llm", "model",     T113CLAW_SECRET_MODEL);
    SET_DEFAULT("llm", "api_url",   T113CLAW_SECRET_API_URL);

    /* If no explicit API URL, derive from provider */
    if (T113CLAW_SECRET_API_URL[0] == '\0') {
        if (strcmp(T113CLAW_SECRET_MODEL_PROVIDER, "claude") == 0) {
            add_entry("llm", "api_url", T113CLAW_CLAUDE_API_URL);
        } else {
            add_entry("llm", "api_url", T113CLAW_OPENAI_API_URL);
        }
    }

    SET_DEFAULT("feishu", "app_id",     T113CLAW_SECRET_FEISHU_APP_ID);
    SET_DEFAULT("feishu", "app_secret", T113CLAW_SECRET_FEISHU_APP_SECRET);

    add_entry("search", "provider", T113CLAW_SEARCH_PROVIDER_DEFAULT);
    add_entry("search", "domestic_only", T113CLAW_SEARCH_DOMESTIC_ONLY_DEFAULT);
    SET_DEFAULT("search", "provider", T113CLAW_SECRET_SEARCH_PROVIDER);
    SET_DEFAULT("search", "domestic_only", T113CLAW_SECRET_SEARCH_DOMESTIC_ONLY);
    SET_DEFAULT("search", "tavily_api_key", T113CLAW_SECRET_TAVILY_API_KEY);

    add_entry("remote", "port", T113CLAW_REMOTE_PORT_DEFAULT);
    SET_DEFAULT("remote", "host", T113CLAW_SECRET_REMOTE_HOST);
    SET_DEFAULT("remote", "port", T113CLAW_SECRET_REMOTE_PORT);
    SET_DEFAULT("remote", "username", T113CLAW_SECRET_REMOTE_USERNAME);
    SET_DEFAULT("remote", "password", T113CLAW_SECRET_REMOTE_PASSWORD);

    SET_DEFAULT("wifi", "ssid", T113CLAW_SECRET_WIFI_SSID);
    SET_DEFAULT("wifi", "pass", T113CLAW_SECRET_WIFI_PASS);

    SET_DEFAULT("proxy", "host", T113CLAW_SECRET_PROXY_HOST);
    SET_DEFAULT("proxy", "port", T113CLAW_SECRET_PROXY_PORT);

    #undef SET_DEFAULT
}

/* ── Public API ───────────────────────────────────────────── */

int config_init(const char *config_path)
{
    pthread_mutex_lock(&s_mutex);

    s_count = 0;

    /* 1. Load build-time defaults (lowest priority) */
    load_defaults();

    /* 2. Override with INI file values (higher priority) */
    int ini_missing = 0;
    if (config_path) {
        struct stat st;
        if (stat(config_path, &st) != 0) {
            ini_missing = 1;
        }
        parse_ini(config_path);
    }

    int count_snap = s_count;
    pthread_mutex_unlock(&s_mutex);

    /* 3. If INI was missing but we have secrets, auto-generate it */
    if (ini_missing && count_snap > 0 && config_path) {
        /* Ensure parent directory exists */
        char dir[256];
        snprintf(dir, sizeof(dir), "%s", config_path);
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            mc_ensure_dir(dir);
        }
        config_save(config_path);
        LOG_I(TAG, "Config auto-generated from build-time secrets: %s", config_path);
    }

    LOG_I(TAG, "Config initialized (%d entries)", count_snap);
    return 0;
}

const char *config_get(const char *section, const char *key)
{
    pthread_mutex_lock(&s_mutex);
    config_entry_t *e = find_entry(section, key);
    const char *val = e ? e->value : NULL;
    pthread_mutex_unlock(&s_mutex);
    return val;
}

const char *config_get_default(const char *section, const char *key, const char *def)
{
    const char *v = config_get(section, key);
    return (v && v[0]) ? v : def;
}

int config_set(const char *section, const char *key, const char *value)
{
    pthread_mutex_lock(&s_mutex);
    int rc = add_entry(section, key, value);
    pthread_mutex_unlock(&s_mutex);
    return rc;
}

int config_save(const char *config_path)
{
    pthread_mutex_lock(&s_mutex);

    FILE *f = fopen(config_path, "w");
    if (!f) {
        pthread_mutex_unlock(&s_mutex);
        LOG_E(TAG, "Cannot write config: %s", config_path);
        return -1;
    }

    fprintf(f, "# T113Claw Configuration (auto-saved)\n\n");

    char last_section[MAX_SEC_LEN] = "";
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_entries[i].section, last_section) != 0) {
            fprintf(f, "\n[%s]\n", s_entries[i].section);
            snprintf(last_section, MAX_SEC_LEN, "%s", s_entries[i].section);
        }
        fprintf(f, "%s = %s\n", s_entries[i].key, s_entries[i].value);
    }

    fclose(f);
    pthread_mutex_unlock(&s_mutex);

    LOG_I(TAG, "Config saved to %s", config_path);
    return 0;
}

/* ── Convenience getters ──────────────────────────────────── */

const char *config_get_api_key(void)
{
    /* Priority: env var > config.ini > build-time secret */
    const char *env = getenv("T113CLAW_API_KEY");
    if (env && env[0]) return env;
    return config_get_default("llm", "api_key", "");
}

const char *config_get_model(void)
{
    return config_get_default("llm", "model", T113CLAW_LLM_DEFAULT_MODEL);
}

const char *config_get_provider(void)
{
    return config_get_default("llm", "provider", T113CLAW_LLM_PROVIDER_DEFAULT);
}

const char *config_get_api_url(void)
{
    return config_get("llm", "api_url");
}

const char *config_get_feishu_app_id(void)
{
    return config_get_default("feishu", "app_id", "");
}

const char *config_get_feishu_app_secret(void)
{
    return config_get_default("feishu", "app_secret", "");
}

const char *config_get_proxy_url(void)
{
    const char *host = config_get("proxy", "host");
    const char *port = config_get("proxy", "port");
    if (!host || !host[0]) return NULL;

    static char proxy_buf[256];
    if (port && port[0])
        snprintf(proxy_buf, sizeof(proxy_buf), "http://%s:%s", host, port);
    else
        snprintf(proxy_buf, sizeof(proxy_buf), "http://%s", host);
    return proxy_buf;
}

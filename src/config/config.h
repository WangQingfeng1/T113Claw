#pragma once
/*
 * T113Claw Configuration Manager
 *
 * Two-layer config: build-time defaults (t113claw_config.h) → INI file overrides.
 * At runtime, values are loaded from config.ini and can be queried by key.
 */

#include <stddef.h>

/* Initialize config: loads config.ini, merges with build-time defaults */
int config_init(const char *config_path);

/* Get a config value by section and key. Returns NULL if not found. */
const char *config_get(const char *section, const char *key);

/* Get with fallback default */
const char *config_get_default(const char *section, const char *key, const char *def);

/* Set a config value at runtime (in-memory only) */
int config_set(const char *section, const char *key, const char *value);

/* Save current config to INI file */
int config_save(const char *config_path);

/* Get commonly used values (convenience wrappers) */
const char *config_get_api_key(void);
const char *config_get_model(void);
const char *config_get_provider(void);
const char *config_get_api_url(void);
const char *config_get_feishu_app_id(void);
const char *config_get_feishu_app_secret(void);
const char *config_get_proxy_url(void);

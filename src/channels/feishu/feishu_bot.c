/*
 * T113Claw Feishu Bot — WebSocket long-connection + REST message send
 *
 * Flow:
 * 1. Acquire tenant_access_token via app_id/app_secret
 * 2. POST to /callback/ws/endpoint to get WSS URL (uses AppID/AppSecret, NOT token)
 * 3. Connect to WSS endpoint
 * 4. Receive protobuf-framed binary events
 * 5. Parse im.message.receive_v1 events → push to inbound message bus
 * 6. ACK each event with original frame metadata + {"code":200}
 * 7. Periodic protobuf ping keep-alive
 * 8. Send replies via REST POST /im/v1/messages (uses Bearer token)
 * 9. Auto-reconnect on disconnect
 */

#include "feishu_bot.h"
#include "feishu_proto.h"
#include "bus/message_bus.h"
#include "config/config.h"
#include "http_client.h"
#include "ws_client.h"
#include "t113claw_config.h"
#include "utils/log.h"
#include "utils/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <cJSON.h>

#define TAG "feishu"

/* ── State ────────────────────────────────────────────────── */

static char s_tenant_token[512];
static int64_t s_token_expire_time = 0;
static pthread_t s_thread;
static volatile bool s_running = false;

/* WebSocket state */
static char s_ws_url[1024];
static int s_ws_service_id = 0;
static int s_ws_ping_interval_s = 120;
static int s_ws_reconnect_interval_s = 30;

/* Deduplication ring buffer */
#define DEDUP_SIZE T113CLAW_FEISHU_DEDUP_SIZE
static uint64_t s_dedup_ring[T113CLAW_FEISHU_DEDUP_SIZE];
static int s_dedup_idx = 0;

/* ── Deduplication ────────────────────────────────────────── */

static uint64_t fnv1a64(const char *s)
{
    uint64_t h = 14695981039346656037ULL;
    if (!s) return h;
    for (; *s; s++) {
        h ^= (uint8_t)*s;
        h *= 1099511628211ULL;
    }
    return h;
}

static bool dedup_check(const char *msg_id)
{
    if (!msg_id || !msg_id[0]) return false;
    uint64_t key = fnv1a64(msg_id);
    for (int i = 0; i < DEDUP_SIZE; i++) {
        if (s_dedup_ring[i] == key) return true;
    }
    s_dedup_ring[s_dedup_idx] = key;
    s_dedup_idx = (s_dedup_idx + 1) % DEDUP_SIZE;
    return false;
}

/* ── Token management ─────────────────────────────────────── */

static int refresh_token(void)
{
    const char *app_id = config_get_feishu_app_id();
    const char *app_secret = config_get_feishu_app_secret();

    if (!app_id[0] || !app_secret[0]) {
        LOG_E(TAG, "Feishu app_id or app_secret not configured");
        return MC_ERR;
    }

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "app_id", app_id);
    cJSON_AddStringToObject(body, "app_secret", app_secret);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    http_response_t resp;
    int rc = http_post_json(T113CLAW_FEISHU_TOKEN_URL, body_str, NULL, &resp);
    free(body_str);

    if (rc != MC_OK || !resp.data) {
        LOG_E(TAG, "Token request failed");
        http_response_free(&resp);
        return MC_ERR;
    }

    cJSON *root = cJSON_Parse(resp.data);
    http_response_free(&resp);
    if (!root) return MC_ERR;

    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (!code || code->valueint != 0) {
        cJSON *msg = cJSON_GetObjectItem(root, "msg");
        LOG_E(TAG, "Token error: code=%d msg=%s",
              code ? code->valueint : -1,
              (msg && msg->valuestring) ? msg->valuestring : "unknown");
        cJSON_Delete(root);
        return MC_ERR;
    }

    cJSON *token = cJSON_GetObjectItem(root, "tenant_access_token");
    cJSON *expire = cJSON_GetObjectItem(root, "expire");

    if (token && token->valuestring) {
        snprintf(s_tenant_token, sizeof(s_tenant_token), "%s", token->valuestring);
        int exp_secs = expire ? expire->valueint : 7200;
        s_token_expire_time = time(NULL) + exp_secs - 300;
        LOG_I(TAG, "Token refreshed (expires in %ds)", exp_secs);
    }

    cJSON_Delete(root);
    return MC_OK;
}

static const char *get_token(void)
{
    if (time(NULL) >= s_token_expire_time) {
        refresh_token();
    }
    return s_tenant_token;
}

/* ── WS config retrieval ──────────────────────────────────── */

static bool parse_query_param(const char *url, const char *key,
                               char *out, size_t out_size)
{
    const char *q = strchr(url, '?');
    if (!q) return false;
    q++;
    size_t key_len = strlen(key);
    while (*q) {
        const char *eq = strchr(q, '=');
        if (!eq) break;
        const char *amp = strchr(eq + 1, '&');
        size_t name_len = (size_t)(eq - q);
        if (name_len == key_len && strncmp(q, key, key_len) == 0) {
            size_t val_len = amp ? (size_t)(amp - (eq + 1)) : strlen(eq + 1);
            size_t n = (val_len < out_size - 1) ? val_len : out_size - 1;
            memcpy(out, eq + 1, n);
            out[n] = '\0';
            return true;
        }
        if (!amp) break;
        q = amp + 1;
    }
    return false;
}

static int pull_ws_config(void)
{
    const char *app_id = config_get_feishu_app_id();
    const char *app_secret = config_get_feishu_app_secret();

    /* NOTE: WS config endpoint uses AppID/AppSecret (PascalCase), NOT tenant_token */
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "AppID", app_id);
    cJSON_AddStringToObject(body, "AppSecret", app_secret);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    const char *headers[] = { "locale: zh", NULL };
    http_response_t resp;
    int rc = http_post_json(T113CLAW_FEISHU_WS_CONFIG_URL, body_str, headers, &resp);
    free(body_str);

    if (rc != MC_OK || !resp.data) {
        LOG_E(TAG, "WS config request failed");
        http_response_free(&resp);
        return MC_ERR;
    }

    LOG_D(TAG, "WS config response: %.200s", resp.data);

    cJSON *root = cJSON_Parse(resp.data);
    http_response_free(&resp);
    if (!root) return MC_ERR;

    cJSON *code = cJSON_GetObjectItem(root, "code");
    cJSON *data = cJSON_GetObjectItem(root, "data");
    cJSON *url_j = data ? cJSON_GetObjectItem(data, "URL") : NULL;
    cJSON *ccfg = data ? cJSON_GetObjectItem(data, "ClientConfig") : NULL;

    if (!code || code->valueint != 0 || !url_j || !cJSON_IsString(url_j)) {
        cJSON *msg = cJSON_GetObjectItem(root, "msg");
        LOG_E(TAG, "WS config error: code=%d msg=%s",
              code ? code->valueint : -1,
              (msg && msg->valuestring) ? msg->valuestring : "unknown");
        cJSON_Delete(root);
        return MC_ERR;
    }

    snprintf(s_ws_url, sizeof(s_ws_url), "%s", url_j->valuestring);

    /* Extract service_id from URL query params */
    char sid[24] = {0};
    if (parse_query_param(s_ws_url, "service_id", sid, sizeof(sid))) {
        s_ws_service_id = atoi(sid);
    }

    /* Parse client config */
    if (ccfg) {
        cJSON *pi = cJSON_GetObjectItem(ccfg, "PingInterval");
        cJSON *ri = cJSON_GetObjectItem(ccfg, "ReconnectInterval");
        if (pi && cJSON_IsNumber(pi)) s_ws_ping_interval_s = pi->valueint;
        if (ri && cJSON_IsNumber(ri)) s_ws_reconnect_interval_s = ri->valueint;
    }

    cJSON_Delete(root);
    LOG_I(TAG, "WS config: service_id=%d ping=%ds reconnect=%ds",
          s_ws_service_id, s_ws_ping_interval_s, s_ws_reconnect_interval_s);
    return MC_OK;
}

/* ── Event handling ───────────────────────────────────────── */

static void handle_message_event(const char *json_str, size_t json_len)
{
    cJSON *root = cJSON_ParseWithLength(json_str, json_len);
    if (!root) return;

    cJSON *header = cJSON_GetObjectItem(root, "header");
    cJSON *event = cJSON_GetObjectItem(root, "event");
    if (!event) {
        cJSON_Delete(root);
        return;
    }

    /* Check event type if header present */
    if (header) {
        cJSON *event_type = cJSON_GetObjectItem(header, "event_type");
        if (!event_type || !event_type->valuestring ||
            strcmp(event_type->valuestring, "im.message.receive_v1") != 0) {
            cJSON_Delete(root);
            return;
        }
    }

    cJSON *message = cJSON_GetObjectItem(event, "message");
    cJSON *sender = cJSON_GetObjectItem(event, "sender");
    if (!message) {
        cJSON_Delete(root);
        return;
    }

    /* Deduplication by message_id */
    cJSON *message_id_j = cJSON_GetObjectItem(message, "message_id");
    if (message_id_j && message_id_j->valuestring) {
        if (dedup_check(message_id_j->valuestring)) {
            LOG_D(TAG, "Duplicate message %s, skipping", message_id_j->valuestring);
            cJSON_Delete(root);
            return;
        }
    }

    /* Only handle text messages */
    cJSON *msg_type = cJSON_GetObjectItem(message, "message_type");
    if (!msg_type || !msg_type->valuestring ||
        strcmp(msg_type->valuestring, "text") != 0) {
        LOG_I(TAG, "Ignoring non-text message type: %s",
              msg_type ? msg_type->valuestring : "null");
        cJSON_Delete(root);
        return;
    }

    /* Parse content JSON (two-layer: content is JSON string inside JSON) */
    cJSON *content = cJSON_GetObjectItem(message, "content");
    if (!content || !content->valuestring) {
        cJSON_Delete(root);
        return;
    }

    cJSON *content_obj = cJSON_Parse(content->valuestring);
    char *text = NULL;
    if (content_obj) {
        cJSON *t = cJSON_GetObjectItem(content_obj, "text");
        if (t && t->valuestring)
            text = strdup(t->valuestring);
        cJSON_Delete(content_obj);
    }

    if (!text || !text[0]) {
        free(text);
        cJSON_Delete(root);
        return;
    }

    /* Strip @mention prefix in group chats */
    char *cleaned = text;
    if (strncmp(cleaned, "@_user_1 ", 9) == 0) {
        cleaned += 9;
    }
    /* Skip leading whitespace */
    while (*cleaned == ' ' || *cleaned == '\n') cleaned++;

    if (!*cleaned) {
        free(text);
        cJSON_Delete(root);
        return;
    }

    /* Determine route_id: P2P → sender open_id, group → chat_id */
    cJSON *chat_type = cJSON_GetObjectItem(message, "chat_type");
    cJSON *chat_id_j = cJSON_GetObjectItem(message, "chat_id");

    const char *sender_id = "";
    if (sender) {
        cJSON *sid = cJSON_GetObjectItem(sender, "sender_id");
        if (sid) {
            cJSON *oid = cJSON_GetObjectItem(sid, "open_id");
            if (oid && oid->valuestring) sender_id = oid->valuestring;
        }
    }

    const char *route_id = "";
    bool is_p2p = (chat_type && chat_type->valuestring &&
                   strcmp(chat_type->valuestring, "p2p") == 0);

    if (is_p2p && sender_id[0]) {
        route_id = sender_id;
    } else if (chat_id_j && chat_id_j->valuestring) {
        route_id = chat_id_j->valuestring;
    }

    LOG_I(TAG, "Message from %s [%s]: %.80s%s",
          sender_id, is_p2p ? "p2p" : "group", cleaned,
          strlen(cleaned) > 80 ? "..." : "");

    /* Push to inbound message bus */
    mc_msg_t msg = {0};
    snprintf(msg.channel, sizeof(msg.channel), "%s", T113CLAW_CHAN_FEISHU);
    snprintf(msg.chat_id, sizeof(msg.chat_id), "%s", route_id);
    msg.content = strdup(cleaned);

    if (msg.content) {
        if (message_bus_push_inbound(&msg) != MC_OK) {
            LOG_W(TAG, "Inbound queue full, dropping feishu message");
            free(msg.content);
        }
    }

    free(text);
    cJSON_Delete(root);
}

/* ── WebSocket frame handling ─────────────────────────────── */

static void handle_ws_frame(ws_client_t *ws, const uint8_t *buf, size_t len)
{
    feishu_frame_t frame;
    if (feishu_proto_parse_frame(buf, len, &frame) != 0) {
        LOG_W(TAG, "Failed to parse WS protobuf frame (%zu bytes)", len);
        return;
    }

    const char *type = feishu_frame_header(&frame, "type");

    /* Control frames (method == 0): ping/pong */
    if (frame.method == 0) {
        if (type && strcmp(type, "pong") == 0 && frame.payload && frame.payload_len > 0) {
            /* Pong may contain updated config */
            cJSON *cfg = cJSON_ParseWithLength((const char *)frame.payload, frame.payload_len);
            if (cfg) {
                cJSON *pi = cJSON_GetObjectItem(cfg, "PingInterval");
                if (pi && cJSON_IsNumber(pi)) {
                    s_ws_ping_interval_s = pi->valueint;
                    LOG_D(TAG, "Ping interval updated to %ds", s_ws_ping_interval_s);
                }
                cJSON_Delete(cfg);
            }
        }
        return;
    }

    /* Only process "event" type frames */
    if (!type || strcmp(type, "event") != 0) return;
    if (!frame.payload || frame.payload_len == 0) return;

    /* Process the event */
    handle_message_event((const char *)frame.payload, frame.payload_len);

    /* Send ACK: reuse original frame metadata, payload = {"code":200} */
    const char *ack_json = "{\"code\":200}";
    uint8_t ack_buf[2048];
    int ack_len = feishu_proto_build_frame(&frame,
                                           (const uint8_t *)ack_json, strlen(ack_json),
                                           ack_buf, sizeof(ack_buf));
    if (ack_len > 0) {
        ws_send_binary(ws, ack_buf, (size_t)ack_len);
    }
}

/* ── WebSocket thread ─────────────────────────────────────── */

static void *feishu_ws_thread(void *arg)
{
    (void)arg;
    LOG_I(TAG, "Feishu bot thread started");

    while (s_running) {
        /* Step 1: Refresh token */
        if (refresh_token() != MC_OK) {
            LOG_E(TAG, "Failed to get Feishu token, retrying in 5s...");
            for (int i = 0; i < 50 && s_running; i++)
                usleep(100000); /* 5s in 100ms chunks */
            continue;
        }

        /* Step 2: Get WS config (re-fetch each reconnect cycle) */
        if (pull_ws_config() != MC_OK) {
            LOG_E(TAG, "Failed to get WS config, retrying in 5s...");
            for (int i = 0; i < 50 && s_running; i++)
                usleep(100000);
            continue;
        }

        /* Step 3: Connect WebSocket */
        LOG_I(TAG, "Connecting to Feishu WebSocket...");
        ws_client_t *ws = ws_connect(s_ws_url);
        if (!ws) {
            LOG_E(TAG, "WebSocket connection failed, retrying in %ds...",
                  s_ws_reconnect_interval_s);
            for (int i = 0; i < s_ws_reconnect_interval_s * 10 && s_running; i++)
                usleep(100000);
            continue;
        }

        LOG_I(TAG, "Feishu WebSocket connected!");

        /* Step 4: Event loop */
        time_t last_ping = time(NULL);
        uint8_t rx_buf[4096];

        while (s_running && ws_is_connected(ws)) {
            /* Receive frames (200ms timeout) */
            size_t rx_len = 0;
            uint8_t opcode = 0;
            int rc = ws_recv(ws, rx_buf, sizeof(rx_buf), &rx_len, &opcode, 200);

            if (rc == 0 && rx_len > 0) {
                if (opcode == 0x02) {
                    /* Binary frame → Feishu protobuf */
                    handle_ws_frame(ws, rx_buf, rx_len);
                } else if (opcode == 0x08) {
                    /* Close frame */
                    LOG_W(TAG, "Server sent close frame");
                    break;
                }
                /* Ping/Pong handled internally by ws_client */
            } else if (rc == -1) {
                LOG_W(TAG, "WebSocket recv error");
                break;
            }

            /* Periodic ping */
            time_t now = time(NULL);
            if (now - last_ping >= s_ws_ping_interval_s) {
                uint8_t ping_buf[256];
                int ping_len = feishu_proto_build_ping(s_ws_service_id,
                                                       ping_buf, sizeof(ping_buf));
                if (ping_len > 0) {
                    ws_send_binary(ws, ping_buf, (size_t)ping_len);
                    LOG_D(TAG, "Sent protobuf ping (service=%d)", s_ws_service_id);
                }
                last_ping = now;
            }

            /* Proactive token refresh */
            if (time(NULL) >= s_token_expire_time - 600) {
                refresh_token();
            }
        }

        /* Cleanup and reconnect */
        ws_close(ws);
        LOG_W(TAG, "Feishu WebSocket disconnected, reconnecting in %ds...",
              s_ws_reconnect_interval_s);

        for (int i = 0; i < s_ws_reconnect_interval_s * 10 && s_running; i++)
            usleep(100000);
    }

    LOG_I(TAG, "Feishu bot thread exited");
    return NULL;
}

/* ── Public API ───────────────────────────────────────────── */

int feishu_bot_init(void)
{
    memset(s_tenant_token, 0, sizeof(s_tenant_token));
    memset(s_dedup_ring, 0, sizeof(s_dedup_ring));
    memset(s_ws_url, 0, sizeof(s_ws_url));
    s_token_expire_time = 0;
    s_dedup_idx = 0;
    s_ws_service_id = 0;
    s_ws_ping_interval_s = 120;
    s_ws_reconnect_interval_s = 30;

    LOG_I(TAG, "Feishu bot initialized");
    return MC_OK;
}

int feishu_bot_start(void)
{
    const char *app_id = config_get_feishu_app_id();
    if (!app_id || !app_id[0]) {
        LOG_W(TAG, "Feishu app_id not configured, skipping");
        return MC_OK;
    }

    s_running = true;
    int rc = pthread_create(&s_thread, NULL, feishu_ws_thread, NULL);
    if (rc != 0) {
        LOG_E(TAG, "Failed to create Feishu thread");
        return MC_ERR;
    }

    pthread_setname_np(s_thread, "feishu_bot");
    LOG_I(TAG, "Feishu WebSocket mode enabled");
    return MC_OK;
}

void feishu_bot_stop(void)
{
    if (!s_running) return;
    s_running = false;
    pthread_join(s_thread, NULL);
    LOG_I(TAG, "Feishu bot stopped");
}

int feishu_bot_send(const char *chat_id, const char *text)
{
    const char *token = get_token();
    if (!token[0]) {
        LOG_E(TAG, "No token available for sending");
        return MC_ERR;
    }

    /* Determine receive_id_type: ou_ = open_id, oc_ = chat_id */
    const char *id_type = "open_id";
    if (strncmp(chat_id, "oc_", 3) == 0) {
        id_type = "chat_id";
    }

    /* Build content JSON: {"text":"..."} */
    cJSON *content = cJSON_CreateObject();
    cJSON_AddStringToObject(content, "text", text);
    char *content_str = cJSON_PrintUnformatted(content);
    cJSON_Delete(content);
    if (!content_str) return MC_ERR;

    /* Build request body */
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "receive_id", chat_id);
    cJSON_AddStringToObject(body, "msg_type", "text");
    cJSON_AddStringToObject(body, "content", content_str);
    free(content_str);

    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_str) return MC_ERR;

    /* URL with receive_id_type */
    char url[512];
    snprintf(url, sizeof(url), "%s?receive_id_type=%s",
             T113CLAW_FEISHU_SEND_MSG_URL, id_type);

    /* Headers */
    char auth[600];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
    const char *headers[] = { auth, NULL };

    http_response_t resp;
    int rc = http_post_json(url, body_str, headers, &resp);
    free(body_str);

    if (rc != MC_OK) {
        LOG_E(TAG, "Send message request failed");
        http_response_free(&resp);
        return MC_ERR;
    }

    /* Check response */
    int result = MC_OK;
    if (resp.data) {
        cJSON *root = cJSON_Parse(resp.data);
        if (root) {
            cJSON *code = cJSON_GetObjectItem(root, "code");
            if (code && code->valueint != 0) {
                cJSON *msg = cJSON_GetObjectItem(root, "msg");
                LOG_E(TAG, "Send msg error: code=%d msg=%s",
                      code->valueint,
                      (msg && msg->valuestring) ? msg->valuestring : "unknown");
                result = MC_ERR;
            } else {
                LOG_D(TAG, "Message sent to %s", chat_id);
            }
            cJSON_Delete(root);
        }
    }

    http_response_free(&resp);
    return result;
}

/* ── Channel descriptor ───────────────────────────────────── */

static const mc_channel_t s_feishu_descriptor = {
    .name  = T113CLAW_CHAN_FEISHU,
    .init  = feishu_bot_init,
    .start = feishu_bot_start,
    .stop  = feishu_bot_stop,
    .send  = feishu_bot_send,
};

const mc_channel_t *feishu_bot_descriptor(void)
{
    return &s_feishu_descriptor;
}

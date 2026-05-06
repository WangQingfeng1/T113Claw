#pragma once
/*
 * T113Claw — Global Configuration Constants
 *
 * Compile-time defaults. Runtime overrides via config.ini or CLI.
 */

#define T113CLAW_VERSION "0.1.0"

/* ── Build-time secrets (highest priority) ──────────────────── */
#if __has_include("t113claw_secrets.h")
#include "t113claw_secrets.h"
#endif

#ifndef T113CLAW_SECRET_WIFI_SSID
#define T113CLAW_SECRET_WIFI_SSID         ""
#endif
#ifndef T113CLAW_SECRET_WIFI_PASS
#define T113CLAW_SECRET_WIFI_PASS         ""
#endif
#ifndef T113CLAW_SECRET_API_KEY
#define T113CLAW_SECRET_API_KEY           ""
#endif
#ifndef T113CLAW_SECRET_MODEL
#define T113CLAW_SECRET_MODEL             ""
#endif
#ifndef T113CLAW_SECRET_MODEL_PROVIDER
#define T113CLAW_SECRET_MODEL_PROVIDER    "openai"
#endif
#ifndef T113CLAW_SECRET_API_URL
#define T113CLAW_SECRET_API_URL           ""
#endif
#ifndef T113CLAW_SECRET_FEISHU_APP_ID
#define T113CLAW_SECRET_FEISHU_APP_ID     ""
#endif
#ifndef T113CLAW_SECRET_FEISHU_APP_SECRET
#define T113CLAW_SECRET_FEISHU_APP_SECRET ""
#endif
#ifndef T113CLAW_SECRET_XFYUN_APPID
#define T113CLAW_SECRET_XFYUN_APPID       ""
#endif
#ifndef T113CLAW_SECRET_XFYUN_APIKEY
#define T113CLAW_SECRET_XFYUN_APIKEY      ""
#endif
#ifndef T113CLAW_SECRET_XFYUN_APISECRET
#define T113CLAW_SECRET_XFYUN_APISECRET   ""
#endif
#ifndef T113CLAW_SECRET_PROXY_HOST
#define T113CLAW_SECRET_PROXY_HOST        ""
#endif
#ifndef T113CLAW_SECRET_PROXY_PORT
#define T113CLAW_SECRET_PROXY_PORT        ""
#endif
#ifndef T113CLAW_SECRET_SEARCH_PROVIDER
#define T113CLAW_SECRET_SEARCH_PROVIDER    ""
#endif
#ifndef T113CLAW_SECRET_SEARCH_DOMESTIC_ONLY
#define T113CLAW_SECRET_SEARCH_DOMESTIC_ONLY ""
#endif
#ifndef T113CLAW_SECRET_TAVILY_API_KEY
#define T113CLAW_SECRET_TAVILY_API_KEY     ""
#endif
#ifndef T113CLAW_SECRET_REMOTE_HOST
#define T113CLAW_SECRET_REMOTE_HOST       ""
#endif
#ifndef T113CLAW_SECRET_REMOTE_PORT
#define T113CLAW_SECRET_REMOTE_PORT       ""
#endif
#ifndef T113CLAW_SECRET_REMOTE_USERNAME
#define T113CLAW_SECRET_REMOTE_USERNAME   ""
#endif
#ifndef T113CLAW_SECRET_REMOTE_PASSWORD
#define T113CLAW_SECRET_REMOTE_PASSWORD   ""
#endif

/* ── Data paths ─────────────────────────────────────────────── */
#ifndef T113CLAW_DATA_DIR
#define T113CLAW_DATA_DIR                 "./data"
#endif
#define T113CLAW_CONFIG_DIR               T113CLAW_DATA_DIR "/config"
#define T113CLAW_MEMORY_DIR               T113CLAW_DATA_DIR "/memory"
#define T113CLAW_SESSION_DIR              T113CLAW_DATA_DIR "/sessions"
#define T113CLAW_SKILLS_DIR               T113CLAW_DATA_DIR "/skills"
#define T113CLAW_UI_DIR                   T113CLAW_DATA_DIR "/ui"
#define T113CLAW_UI_FONT_DIR              T113CLAW_UI_DIR "/font"
#define T113CLAW_UI_IMAGE_DIR             T113CLAW_UI_DIR "/image"

#define T113CLAW_CONFIG_FILE              T113CLAW_CONFIG_DIR "/config.ini"
#define T113CLAW_SOUL_FILE                T113CLAW_MEMORY_DIR "/SOUL.md"
#define T113CLAW_USER_FILE                T113CLAW_MEMORY_DIR "/USER.md"
#define T113CLAW_MEMORY_FILE              T113CLAW_MEMORY_DIR "/MEMORY.md"
#define T113CLAW_HEARTBEAT_FILE           T113CLAW_DATA_DIR "/HEARTBEAT.md"
#define T113CLAW_CRON_FILE                T113CLAW_DATA_DIR "/cron.json"
#define T113CLAW_UI_FONT_REGULAR          T113CLAW_UI_FONT_DIR "/SOURCEHANSANSCN_REGULAR.OTF"
#define T113CLAW_UI_ICON_BACK             T113CLAW_UI_IMAGE_DIR "/icon_back_pixel.png"
#define T113CLAW_UI_ICON_SETTINGS         T113CLAW_UI_IMAGE_DIR "/icon_settings_pixel.png"
#define T113CLAW_UI_ICON_WIFI_SETTING     T113CLAW_UI_IMAGE_DIR "/icon_wifi_setting_pixel.png"
#define T113CLAW_UI_ICON_SERVER_SETTING   T113CLAW_UI_IMAGE_DIR "/icon_server_setting_pixel.png"
#define T113CLAW_UI_ICON_SYSTEM_SETTING   T113CLAW_UI_IMAGE_DIR "/icon_system_setting_pixel.png"
#define T113CLAW_UI_ICON_SERVER_ON        T113CLAW_UI_IMAGE_DIR "/icon_server_on_pixel.png"
#define T113CLAW_UI_ICON_SERVER_OFF       T113CLAW_UI_IMAGE_DIR "/icon_server_off_pixel.png"
#define T113CLAW_UI_ICON_SERVER_CONNECTING T113CLAW_UI_IMAGE_DIR "/icon_server_connecting_pixel.png"
#define T113CLAW_UI_ICON_SERVER_FAILED    T113CLAW_UI_IMAGE_DIR "/icon_server_failed_pixel.png"
#define T113CLAW_UI_ICON_WIFI_ON          T113CLAW_UI_IMAGE_DIR "/icon_wifi_on_pixel.png"
#define T113CLAW_UI_ICON_WIFI_OFF         T113CLAW_UI_IMAGE_DIR "/icon_wifi_off_pixel.png"
#define T113CLAW_UI_ICON_WIFI_CONNECTING  T113CLAW_UI_IMAGE_DIR "/icon_wifi_connecting_pixel.png"
#define T113CLAW_UI_ICON_WIFI_FAILED      T113CLAW_UI_IMAGE_DIR "/icon_wifi_failed_pixel.png"
#define T113CLAW_UI_ICON_VOICE_IDLE       T113CLAW_UI_IMAGE_DIR "/icon_voice_idle_pixel.png"
#define T113CLAW_UI_ICON_VOICE_ACTIVE     T113CLAW_UI_IMAGE_DIR "/icon_voice_active_pixel.png"
#define T113CLAW_UI_ICON_VOICE_LISTENING  T113CLAW_UI_IMAGE_DIR "/icon_voice_listening_pixel.png"
#define T113CLAW_UI_ICON_VOICE_RECOGNIZING T113CLAW_UI_IMAGE_DIR "/icon_voice_recognizing_pixel.png"
#define T113CLAW_UI_ICON_VOICE_SPEAKING   T113CLAW_UI_IMAGE_DIR "/icon_voice_speaking_pixel.png"
#define T113CLAW_UI_ICON_AGENT            T113CLAW_UI_IMAGE_DIR "/icon_agent_thinking_pixel.png"
#define T113CLAW_UI_ICON_AGENT_READY      T113CLAW_UI_IMAGE_DIR "/icon_agent_ready_pixel.png"
#define T113CLAW_UI_ICON_AGENT_SUCCESS    T113CLAW_UI_IMAGE_DIR "/icon_agent_success_pixel.png"
#define T113CLAW_UI_ICON_AGENT_ERROR      T113CLAW_UI_IMAGE_DIR "/icon_agent_error_pixel.png"
#define T113CLAW_UI_ICON_VOLUME_ON        T113CLAW_UI_IMAGE_DIR "/icon_volume_on_pixel.png"
#define T113CLAW_UI_AVATAR_MAIN           T113CLAW_UI_IMAGE_DIR "/avatar_t113claw_pixel.png"
#define T113CLAW_UI_BG_CHAT               T113CLAW_UI_IMAGE_DIR "/bg_chat_loop.png"
#define T113CLAW_UI_BG_SETTINGS           T113CLAW_UI_IMAGE_DIR "/bg_settings_matrix.png"

/* ── Message Bus ────────────────────────────────────────────── */
#define T113CLAW_BUS_QUEUE_LEN            16

/* ── Agent Loop ─────────────────────────────────────────────── */
#define T113CLAW_AGENT_MAX_HISTORY        20
#define T113CLAW_AGENT_MAX_TOOL_ITER      10
#define T113CLAW_MAX_TOOL_CALLS           4

/* ── LLM ────────────────────────────────────────────────────── */
#define T113CLAW_LLM_DEFAULT_MODEL        "gpt-4o"
#define T113CLAW_LLM_PROVIDER_DEFAULT     "openai"
#define T113CLAW_LLM_MAX_TOKENS           4096
#define T113CLAW_OPENAI_API_URL           "https://api.openai.com/v1/chat/completions"
#define T113CLAW_CLAUDE_API_URL           "https://api.anthropic.com/v1/messages"
#define T113CLAW_CLAUDE_API_VERSION       "2023-06-01"
#define T113CLAW_LLM_RESPONSE_BUF_SIZE   (64 * 1024)

/* ── Web Search ─────────────────────────────────────────────── */
#define T113CLAW_SEARCH_PROVIDER_DEFAULT   "auto"
#define T113CLAW_SEARCH_DOMESTIC_ONLY_DEFAULT "0"
#define T113CLAW_SEARCH_SOGOU_URL          "https://m.sogou.com/web/searchList.jsp"
#define T113CLAW_SEARCH_TAVILY_URL         "https://api.tavily.com/search"
#define T113CLAW_SEARCH_MAX_RESULTS        5
#define T113CLAW_SEARCH_MAX_QUERY_LEN      256
#define T113CLAW_SEARCH_MAX_SITE_LEN       128

/* ── Remote Control ─────────────────────────────────────────── */
#define T113CLAW_REMOTE_PORT_DEFAULT       "8765"
#define T113CLAW_REMOTE_EXEC_PATH          "/exec"
#define T113CLAW_REMOTE_STATUS_PATH        "/status"
#define T113CLAW_REMOTE_DEFAULT_TIMEOUT_S  30
#define T113CLAW_REMOTE_MAX_TIMEOUT_S      120
#define T113CLAW_REMOTE_OUTPUT_TEXT_MAX    4096

/* ── Context ────────────────────────────────────────────────── */
#define T113CLAW_CONTEXT_BUF_SIZE         (16 * 1024)
#define T113CLAW_SESSION_MAX_MSGS         20

/* ── Feishu Bot ─────────────────────────────────────────────── */
#define T113CLAW_FEISHU_MAX_MSG_LEN       4096
#define T113CLAW_FEISHU_TOKEN_URL         "https://open.feishu.cn/open-apis/auth/v3/tenant_access_token/internal"
#define T113CLAW_FEISHU_WS_CONFIG_URL     "https://open.feishu.cn/callback/ws/endpoint"
#define T113CLAW_FEISHU_SEND_MSG_URL      "https://open.feishu.cn/open-apis/im/v1/messages"
#define T113CLAW_FEISHU_DEDUP_SIZE        64

/* ── Channels ───────────────────────────────────────────────── */
#define T113CLAW_CHAN_FEISHU              "feishu"
#define T113CLAW_CHAN_CLI                 "cli"
#define T113CLAW_CHAN_SYSTEM              "system"
#define T113CLAW_CHAN_UI                  "ui"
#define T113CLAW_CHAN_VOICE               "voice"

/* ── Cron / Heartbeat ───────────────────────────────────────── */
#define T113CLAW_CRON_MAX_JOBS            16
#define T113CLAW_CRON_CHECK_INTERVAL_S    60
#define T113CLAW_HEARTBEAT_INTERVAL_S     (30 * 60)

/* ── Timezone ───────────────────────────────────────────────── */
#define T113CLAW_TIMEZONE                 "CST-8"

/* ── Tool output buffer ─────────────────────────────────────── */
#define T113CLAW_TOOL_OUTPUT_SIZE         8192

/* ── Memory read buffer ─────────────────────────────────────── */
#define T113CLAW_MEMORY_RECENT_DAYS       3

/* ── Voice ───────────────────────────────────────────────────── */
#define T113CLAW_BUTTON_GPIO              106     /* PD10 = 96 + 10 */
#define T113CLAW_TTS_VOICE                "xiaoyan"

/* ── Wake Word (sherpa-onnx KWS) ─────────────────────────────── */
#define T113CLAW_WAKE_WORD                "你好小爪"
#ifndef T113CLAW_KWS_MODEL_DIR
#define T113CLAW_KWS_MODEL_DIR            "/usr/share/t113claw/kws-model"
#endif
#ifndef T113CLAW_KWS_KEYWORDS_FILE
#define T113CLAW_KWS_KEYWORDS_FILE        T113CLAW_KWS_MODEL_DIR "/keywords_t113claw.txt"
#endif

#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "lv_freetype.h"

#define UI_FS_SRC(path) "A:" path

#define UI_CHAT_HISTORY 24
#define UI_TEXT_MAX    2048

typedef enum {
    UI_PAGE_CHAT = 0,
    UI_PAGE_SETTINGS,
    UI_PAGE_COUNT,
} ui_page_t;

typedef enum {
    UI_SERVER_LINK_UNCONFIGURED = 0,
    UI_SERVER_LINK_CONNECTING,
    UI_SERVER_LINK_ONLINE,
    UI_SERVER_LINK_FAILED,
} ui_server_link_state_t;

typedef enum {
    UI_CHAT_ROLE_SYSTEM = 0,
    UI_CHAT_ROLE_USER,
    UI_CHAT_ROLE_ASSISTANT,
    UI_CHAT_ROLE_EVENT,
} ui_chat_role_t;

typedef struct {
    ui_chat_role_t role;
    char channel[32];
    char stamp[16];
    char text[UI_TEXT_MAX];
} ui_chat_entry_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *header_clock;
    lv_obj_t *header_title;
    lv_obj_t *header_wifi_icon;
    lv_obj_t *header_server_icon;
    lv_obj_t *pages[UI_PAGE_COUNT];

    lv_obj_t *chat_feed;
    lv_obj_t *chat_total_value;
    lv_obj_t *chat_hint_value;
    lv_obj_t *chat_follow_label;
    lv_obj_t *chat_error_value;
    lv_obj_t *chat_voice_icon;
    lv_obj_t *chat_agent_icon;
    lv_obj_t *chat_voice_state;
    lv_obj_t *chat_agent_state;

    lv_style_t screen_style;
    lv_style_t panel_style;
    lv_style_t panel_alt_style;
    lv_style_t card_style;
    lv_style_t card_warn_style;
    lv_style_t badge_style;
    lv_style_t badge_alert_style;
    lv_style_t text_dim_style;
    lv_style_t text_value_style;
    lv_style_t message_user_style;
    lv_style_t message_ai_style;

    lv_ft_info_t font_small_info;
    lv_ft_info_t font_medium_info;
    lv_ft_info_t font_large_info;
    const lv_font_t *font_cn_small;
    const lv_font_t *font_cn_medium;
    const lv_font_t *font_cn_large;
    const lv_font_t *font_pixel_small;
    const lv_font_t *font_pixel_large;
    bool fonts_ready;

    ui_chat_entry_t chat_entries[UI_CHAT_HISTORY];
    char current_channel[32];
    char voice_state[32];
    char agent_state[32];
    char chat_error_source[32];
    char chat_error_text[160];
    unsigned int chat_user_count;
    unsigned int chat_assistant_count;
    unsigned int chat_system_count;
    int chat_entry_count;
    bool chat_follow_tail;
    ui_page_t current_page;
    ui_server_link_state_t server_link_state;
    char server_link_text[160];
} ui_context_t;

void ui_theme_init(ui_context_t *ui);
void ui_theme_deinit(ui_context_t *ui);
int ui_font_init(ui_context_t *ui);
void ui_font_deinit(ui_context_t *ui);
void ui_page_show(ui_context_t *ui, ui_page_t page);
void ui_set_header_clock(ui_context_t *ui, const char *text);
lv_obj_t *ui_create_png_image(lv_obj_t *parent, const char *src, uint16_t zoom);
void ui_set_png_image_src(lv_obj_t *image, const char *src, uint16_t zoom);
const char *ui_voice_icon_src(const char *state);
const char *ui_agent_icon_src(const char *state);

void page_chat_build(ui_context_t *ui, lv_obj_t *parent);
void page_chat_append(ui_context_t *ui, const char *role, const char *channel, const char *text);
void page_chat_set_voice_state(ui_context_t *ui, const char *state);
void page_chat_set_agent_state(ui_context_t *ui, const char *state);
void page_chat_set_runtime_error(ui_context_t *ui, const char *source, const char *text);
void page_chat_clear_runtime_error(ui_context_t *ui, const char *source);

void page_settings_build(ui_context_t *ui, lv_obj_t *parent);
void page_settings_refresh(ui_context_t *ui);
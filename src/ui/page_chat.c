#include "ui_private.h"
#include "t113claw_config.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define CHAT_PANEL_LEFT_W            156
#define CHAT_PANEL_CENTER_W          0
#define CHAT_PANEL_RIGHT_W           248
#define CHAT_PANEL_PAD               6
#define CHAT_PANEL_GAP               4
#define CHAT_AVATAR_SIZE             96
#define CHAT_AVATAR_ZOOM             236
#define CHAT_FEED_HEIGHT             158
#define CHAT_HEADER_HEIGHT           14
#define CHAT_STATE_ICON_ZOOM         220
#define CHAT_STATE_TILE_W            0
#define CHAT_ACTION_BTN_W            84
#define CHAT_ACTION_BTN_H            18
#define CHAT_ERROR_TEXT_H            12
#define CHAT_FEED_BUBBLE_USER_W      340
#define CHAT_FEED_BUBBLE_ASSIST_W    430
#define CHAT_FEED_BUBBLE_SYSTEM_W    500

static lv_obj_t *create_panel(lv_obj_t *parent, lv_style_t *style, lv_coord_t width)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_width(panel, width);
    lv_obj_set_height(panel, LV_PCT(100));
    lv_obj_add_style(panel, style, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return panel;
}

static void compact_panel(lv_obj_t *panel, lv_coord_t pad, lv_coord_t gap)
{
    lv_obj_set_style_pad_all(panel, pad, 0);
    lv_obj_set_style_pad_gap(panel, gap, 0);
}

static size_t chat_utf8_char_len(unsigned char ch)
{
    if ((ch & 0x80) == 0x00) return 1;
    if ((ch & 0xE0) == 0xC0) return 2;
    if ((ch & 0xF0) == 0xE0) return 3;
    if ((ch & 0xF8) == 0xF0) return 4;
    return 1;
}

static bool chat_is_markdown_list_marker(const unsigned char *cursor)
{
    const unsigned char *scan = cursor;

    while (*scan == ' ') {
        scan++;
    }

    if ((*scan == '-' || *scan == '*' || *scan == '+' || *scan == '>') && scan[1] == ' ') {
        return true;
    }

    if (*scan == '#') {
        while (*scan == '#') {
            scan++;
        }
        return *scan == ' ';
    }

    if (*scan >= '0' && *scan <= '9') {
        while (*scan >= '0' && *scan <= '9') {
            scan++;
        }
        return *scan == '.' && scan[1] == ' ';
    }

    return false;
}

static const unsigned char *chat_skip_markdown_marker(const unsigned char *cursor)
{
    while (*cursor == ' ') {
        cursor++;
    }

    if ((*cursor == '-' || *cursor == '*' || *cursor == '+' || *cursor == '>') && cursor[1] == ' ') {
        return cursor + 2;
    }

    if (*cursor == '#') {
        while (*cursor == '#') {
            cursor++;
        }
        while (*cursor == ' ') {
            cursor++;
        }
        return cursor;
    }

    if (*cursor >= '0' && *cursor <= '9') {
        while (*cursor >= '0' && *cursor <= '9') {
            cursor++;
        }
        if (*cursor == '.' && cursor[1] == ' ') {
            cursor += 2;
        }
    }

    return cursor;
}

static bool chat_is_filtered_symbol(const unsigned char *cursor, size_t char_len)
{
    if (char_len == 4) {
        return true;
    }

    if (char_len == 3 && cursor[0] == 0xE2) {
        if (cursor[1] == 0x98 || cursor[1] == 0x99 || cursor[1] == 0x9C ||
            cursor[1] == 0x9D || cursor[1] == 0x9E || cursor[1] == 0x9F) {
            return true;
        }
    }

    return false;
}

static void chat_sanitize_assistant_text(const char *input, char *output, size_t output_size)
{
    const unsigned char *cursor = (const unsigned char *)input;
    size_t out = 0;
    bool line_start = true;

    if (!input || !output || output_size == 0) return;

    while (*cursor && out + 1 < output_size) {
        size_t char_len = chat_utf8_char_len(*cursor);

        if (*cursor == '\r') {
            cursor++;
            continue;
        }

        if (*cursor == '\n') {
            output[out++] = (char)*cursor++;
            line_start = true;
            continue;
        }

        if (line_start && chat_is_markdown_list_marker(cursor)) {
            cursor = chat_skip_markdown_marker(cursor);
            line_start = false;
            continue;
        }

        if (*cursor == '*' || *cursor == '_' || *cursor == '`' || *cursor == '~') {
            cursor++;
            continue;
        }

        if (chat_is_filtered_symbol(cursor, char_len)) {
            cursor += char_len;
            continue;
        }

        if (out + char_len >= output_size) {
            break;
        }

        for (size_t i = 0; i < char_len; i++) {
            output[out++] = (char)cursor[i];
        }
        cursor += char_len;
        line_start = false;
    }

    output[out] = '\0';
}

static void chat_timestamp_now(char *buf, size_t size)
{
    time_t now = time(NULL);
    struct tm tm_now;

    localtime_r(&now, &tm_now);
    strftime(buf, size, "%H:%M:%S", &tm_now);
}

static const char *chat_role_title(ui_chat_role_t role)
{
    switch (role) {
    case UI_CHAT_ROLE_USER:
        return "ME";
    case UI_CHAT_ROLE_ASSISTANT:
        return "AI";
    case UI_CHAT_ROLE_EVENT:
        return "RUN";
    case UI_CHAT_ROLE_SYSTEM:
    default:
        return "SYS";
    }
}

static lv_style_t *chat_role_style(ui_context_t *ui, ui_chat_role_t role)
{
    switch (role) {
    case UI_CHAT_ROLE_USER:
        return &ui->message_user_style;
    case UI_CHAT_ROLE_ASSISTANT:
        return &ui->message_ai_style;
    case UI_CHAT_ROLE_EVENT:
        return &ui->badge_alert_style;
    case UI_CHAT_ROLE_SYSTEM:
    default:
        return &ui->badge_style;
    }
}

static void chat_refresh_overview(ui_context_t *ui)
{
    if (ui->chat_total_value) {
        lv_label_set_text_fmt(ui->chat_total_value,
                              "ME %u\nAI %u\nROUTE %s",
                              ui->chat_user_count,
                              ui->chat_assistant_count,
                              ui->current_channel[0] ? ui->current_channel : "cli");
    }

    if (ui->chat_hint_value) {
        if (ui->chat_entry_count == 0) {
            lv_label_set_text(ui->chat_hint_value,
                              "Waiting for Me / AI turns.");
        } else if (ui->chat_follow_tail) {
            lv_label_set_text(ui->chat_hint_value,
                              "TAIL follows newest.");
        } else {
            lv_label_set_text(ui->chat_hint_value,
                              "TAIL paused.");
        }
    }

    if (ui->chat_follow_label) {
        lv_label_set_text(ui->chat_follow_label, ui->chat_follow_tail ? "TAIL ON" : "TAIL OFF");
    }
}

static bool chat_state_has_error(const char *state)
{
    if (!state || state[0] == '\0') return false;
    return strstr(state, "ERROR") || strstr(state, "FAILED") || strstr(state, "FAIL");
}

static bool chat_error_source_matches(const char *source, const char *group)
{
    if (!source || source[0] == '\0') return false;
    if (!group || group[0] == '\0') return true;
    if (strcmp(source, group) == 0) return true;

    if (strcmp(group, "voice") == 0) {
        return strcmp(source, "stt") == 0 ||
               strcmp(source, "tts") == 0 ||
               strcmp(source, "vad") == 0 ||
               strcmp(source, "wake") == 0 ||
               strcmp(source, "gpio") == 0 ||
               strcmp(source, "button") == 0;
    }

    if (strcmp(group, "agent") == 0) {
        return strcmp(source, "llm") == 0;
    }

    return false;
}

static void chat_refresh_runtime_error(ui_context_t *ui)
{
    if (!ui || !ui->chat_error_value) return;

    if (ui->chat_error_text[0] == '\0') {
        lv_label_set_text(ui->chat_error_value, "");
        lv_obj_add_flag(ui->chat_error_value, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(ui->chat_error_value, ui->chat_error_text);
    lv_obj_clear_flag(ui->chat_error_value, LV_OBJ_FLAG_HIDDEN);
}

static void chat_scroll_to_tail(ui_context_t *ui)
{
    uint32_t child_count;
    lv_obj_t *last_child;

    if (!ui->chat_feed) return;

    child_count = lv_obj_get_child_cnt(ui->chat_feed);
    if (child_count == 0) return;

    last_child = lv_obj_get_child(ui->chat_feed, child_count - 1);
    if (last_child) {
        lv_obj_scroll_to_view(last_child, LV_ANIM_OFF);
    }
}

static void chat_rebuild_feed(ui_context_t *ui)
{
    if (!ui->chat_feed) return;

    lv_obj_clean(ui->chat_feed);

    if (ui->chat_entry_count == 0) {
        lv_obj_t *empty_wrap = lv_obj_create(ui->chat_feed);
        lv_obj_t *empty = lv_label_create(empty_wrap);

        lv_obj_remove_style_all(empty_wrap);
        lv_obj_set_width(empty_wrap, LV_PCT(100));
        lv_obj_set_flex_grow(empty_wrap, 1);
        lv_obj_set_layout(empty_wrap, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(empty_wrap, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(empty_wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(empty_wrap, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_font(empty, ui->font_pixel_small, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0x9fb8c8), 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
        lv_label_set_text(empty, "No messages yet. Waiting for Me / AI turns.");
        return;
    }

    for (int i = 0; i < ui->chat_entry_count; i++) {
        const ui_chat_entry_t *entry = &ui->chat_entries[i];
        const bool is_user = entry->role == UI_CHAT_ROLE_USER;
        lv_coord_t bubble_width = CHAT_FEED_BUBBLE_ASSIST_W;

        if (entry->role == UI_CHAT_ROLE_USER) {
            bubble_width = CHAT_FEED_BUBBLE_USER_W;
        } else if (entry->role == UI_CHAT_ROLE_SYSTEM || entry->role == UI_CHAT_ROLE_EVENT) {
            bubble_width = CHAT_FEED_BUBBLE_SYSTEM_W;
        }

        lv_obj_t *row = lv_obj_create(ui->chat_feed);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row,
                      is_user ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                      LV_FLEX_ALIGN_CENTER,
                      LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);

        lv_obj_t *bubble = lv_obj_create(row);
        lv_obj_set_size(bubble, bubble_width, LV_SIZE_CONTENT);
        lv_obj_add_style(bubble, chat_role_style(ui, entry->role), 0);
        lv_obj_set_layout(bubble, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(bubble, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_all(bubble, 5, 0);
        lv_obj_set_style_pad_gap(bubble, 3, 0);
        lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(bubble, LV_OBJ_FLAG_GESTURE_BUBBLE);

        char meta[96];
        if (entry->channel[0]) {
            snprintf(meta, sizeof(meta), "%s  %s  %s",
                     chat_role_title(entry->role), entry->channel, entry->stamp);
        } else {
            snprintf(meta, sizeof(meta), "%s  %s",
                     chat_role_title(entry->role), entry->stamp);
        }

        lv_obj_t *meta_label = lv_label_create(bubble);
        lv_obj_set_width(meta_label, LV_PCT(100));
        lv_obj_set_style_text_font(meta_label, ui->font_pixel_small, 0);
        lv_obj_set_style_text_color(meta_label, lv_color_hex(0x9fd1de), 0);
        lv_label_set_text(meta_label, meta);

        lv_obj_t *text_label = lv_label_create(bubble);
        lv_obj_set_width(text_label, LV_PCT(100));
        lv_label_set_long_mode(text_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(text_label, ui->font_cn_small, 0);
        lv_label_set_text(text_label, entry->text);
    }

    lv_obj_update_layout(ui->chat_feed);

    if (ui->chat_follow_tail) {
        chat_scroll_to_tail(ui);
    }
}

static void chat_push_entry(ui_context_t *ui,
                            ui_chat_role_t role,
                            const char *channel,
                            const char *text,
                            bool update_current_channel)
{
    ui_chat_entry_t *entry;

    if (!text || text[0] == '\0') return;

    if (role != UI_CHAT_ROLE_USER && role != UI_CHAT_ROLE_ASSISTANT) {
        if (update_current_channel && channel && channel[0]) {
            snprintf(ui->current_channel, sizeof(ui->current_channel), "%s", channel);
        }
        chat_refresh_overview(ui);
        return;
    }

    if (ui->chat_entry_count >= UI_CHAT_HISTORY) {
        memmove(&ui->chat_entries[0],
                &ui->chat_entries[1],
                sizeof(ui->chat_entries[0]) * (UI_CHAT_HISTORY - 1));
        ui->chat_entry_count = UI_CHAT_HISTORY - 1;
    }

    entry = &ui->chat_entries[ui->chat_entry_count++];
    memset(entry, 0, sizeof(*entry));
    entry->role = role;
    if (channel) {
        snprintf(entry->channel, sizeof(entry->channel), "%s", channel);
    }
    chat_timestamp_now(entry->stamp, sizeof(entry->stamp));
    snprintf(entry->text, sizeof(entry->text), "%s", text);

    switch (role) {
    case UI_CHAT_ROLE_USER:
        ui->chat_user_count++;
        break;
    case UI_CHAT_ROLE_ASSISTANT:
        ui->chat_assistant_count++;
        break;
    case UI_CHAT_ROLE_EVENT:
    case UI_CHAT_ROLE_SYSTEM:
    default:
        ui->chat_system_count++;
        break;
    }

    if (update_current_channel && channel && channel[0]) {
        snprintf(ui->current_channel, sizeof(ui->current_channel), "%s", channel);
    }
    chat_refresh_overview(ui);
    chat_rebuild_feed(ui);
}

static void create_avatar(lv_obj_t *parent)
{
    lv_obj_t *frame = lv_obj_create(parent);
    lv_obj_set_size(frame, CHAT_AVATAR_SIZE, CHAT_AVATAR_SIZE);
    lv_obj_set_style_bg_color(frame, lv_color_hex(0x1e3048), 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0x63e6be), 0);
    lv_obj_set_style_border_width(frame, 4, 0);
    lv_obj_set_style_radius(frame, 0, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *avatar = ui_create_png_image(frame, UI_FS_SRC(T113CLAW_UI_AVATAR_MAIN), CHAT_AVATAR_ZOOM);
    lv_obj_center(avatar);
}

static lv_obj_t *create_state_tile(ui_context_t *ui, lv_obj_t *parent,
                                   const char *title, lv_color_t border, const char *icon_src,
                                   lv_obj_t **icon_out, lv_obj_t **label_out)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_size(tile, CHAT_STATE_TILE_W, LV_PCT(100));
    lv_obj_set_flex_grow(tile, 1);
    lv_obj_add_style(tile, &ui->card_style, 0);
    lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_color(tile, border, 0);
    lv_obj_set_style_pad_all(tile, 6, 0);
    lv_obj_set_style_pad_gap(tile, 4, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *title_label = lv_label_create(tile);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_obj_set_style_text_font(title_label, ui->font_pixel_small, 0);
    lv_obj_set_style_text_color(title_label, border, 0);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title_label, title);

    lv_obj_t *icon = ui_create_png_image(tile, icon_src, CHAT_STATE_ICON_ZOOM);

    lv_obj_t *label = lv_label_create(tile);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, ui->font_cn_small, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xf3f7fb), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    if (icon_out) *icon_out = icon;
    if (label_out) *label_out = label;
    return tile;
}

static lv_obj_t *create_info_card(ui_context_t *ui, lv_obj_t *parent,
                                  const char *title, lv_obj_t **value_out)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_add_style(card, &ui->badge_style, 0);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(card, 4, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_pad_gap(card, 1, 0);

    lv_obj_t *title_label = lv_label_create(card);
    lv_obj_set_style_text_font(title_label, ui->font_pixel_small, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xf4c95d), 0);
    lv_label_set_text(title_label, title);

    lv_obj_t *value = lv_label_create(card);
    lv_obj_set_width(value, LV_PCT(100));
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(value, ui->font_cn_small, 0);
    lv_obj_set_style_text_color(value, lv_color_hex(0xf3f7fb), 0);

    if (value_out) *value_out = value;
    return card;
}

static void chat_clear_history(ui_context_t *ui, bool add_notice)
{
    memset(ui->chat_entries, 0, sizeof(ui->chat_entries));
    ui->chat_entry_count = 0;
    ui->chat_user_count = 0;
    ui->chat_assistant_count = 0;
    ui->chat_system_count = 0;

    chat_refresh_overview(ui);
    chat_refresh_runtime_error(ui);
    chat_rebuild_feed(ui);

    if (add_notice) {
        chat_push_entry(ui, UI_CHAT_ROLE_SYSTEM, NULL,
                        "Transcript cleared. Waiting for fresh runtime traffic.", false);
    }
}

static void chat_clear_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    chat_clear_history((ui_context_t *)lv_event_get_user_data(event), true);
}

static void chat_clear_error_event_cb(lv_event_t *event)
{
    ui_context_t *ui;

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

    ui = (ui_context_t *)lv_event_get_user_data(event);
    page_chat_clear_runtime_error(ui, NULL);
}

static void chat_follow_event_cb(lv_event_t *event)
{
    ui_context_t *ui;

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

    ui = (ui_context_t *)lv_event_get_user_data(event);
    ui->chat_follow_tail = !ui->chat_follow_tail;
    chat_refresh_overview(ui);
    if (ui->chat_follow_tail) {
        chat_scroll_to_tail(ui);
    }
}

static lv_obj_t *create_action_button(ui_context_t *ui, lv_obj_t *parent,
                                      const char *text, lv_color_t border,
                                      lv_event_cb_t cb, lv_obj_t **label_out)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, CHAT_ACTION_BTN_W, CHAT_ACTION_BTN_H);
    lv_obj_add_style(btn, &ui->badge_style, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x122335), 0);
    lv_obj_set_style_border_color(btn, border, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(btn);
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, ui->font_pixel_small, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xf3f7fb), 0);
    lv_label_set_text(label, text);

    if (label_out) *label_out = label;
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ui);
    return btn;
}

void page_chat_build(ui_context_t *ui, lv_obj_t *parent)
{
    lv_obj_t *traffic_card;
    lv_obj_t *center_actions;
    lv_obj_t *action_group;
    lv_obj_t *state_grid;

    lv_obj_set_style_bg_img_src(parent, UI_FS_SRC(T113CLAW_UI_BG_CHAT), 0);
    lv_obj_set_style_bg_img_opa(parent, LV_OPA_20, 0);
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(parent, 10, 0);

    lv_obj_t *left = create_panel(parent, &ui->panel_style, CHAT_PANEL_LEFT_W);
    compact_panel(left, CHAT_PANEL_PAD, CHAT_PANEL_GAP);
    lv_obj_set_layout(left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    create_avatar(left);

    lv_obj_t *left_title = lv_label_create(left);
    lv_obj_set_style_text_font(left_title, ui->font_pixel_small, 0);
    lv_obj_set_style_text_color(left_title, lv_color_hex(0xf4c95d), 0);
    lv_label_set_text(left_title, "T113CLAW");

    traffic_card = create_info_card(ui, left, "SESSION", &ui->chat_total_value);
    lv_obj_set_style_text_font(ui->chat_total_value, ui->font_pixel_small, 0);
    lv_label_set_long_mode(ui->chat_total_value, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(traffic_card, 1);

    lv_obj_t *center = create_panel(parent, &ui->panel_alt_style, CHAT_PANEL_CENTER_W);
    lv_obj_set_flex_grow(center, 1);
    compact_panel(center, CHAT_PANEL_PAD, CHAT_PANEL_GAP);
    lv_obj_set_layout(center, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(center, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *center_header = lv_obj_create(center);
    lv_obj_remove_style_all(center_header);
    lv_obj_set_width(center_header, LV_PCT(100));
    lv_obj_set_height(center_header, CHAT_HEADER_HEIGHT);
    lv_obj_set_style_pad_all(center_header, 0, 0);
    lv_obj_clear_flag(center_header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *center_title = lv_label_create(center_header);
    lv_obj_set_style_text_font(center_title, ui->font_pixel_small, 0);
    lv_obj_set_style_text_color(center_title, lv_color_hex(0xd8f5ff), 0);
    lv_label_set_text(center_title, "CONVERSATION");
    lv_obj_align(center_title, LV_ALIGN_LEFT_MID, 0, 0);

    ui->chat_feed = lv_obj_create(center);
    lv_obj_set_width(ui->chat_feed, LV_PCT(100));
    lv_obj_set_height(ui->chat_feed, CHAT_FEED_HEIGHT);
    lv_obj_add_style(ui->chat_feed, &ui->card_style, 0);
    lv_obj_set_style_pad_all(ui->chat_feed, 4, 0);
    lv_obj_set_style_pad_gap(ui->chat_feed, 4, 0);
    lv_obj_set_style_bg_color(ui->chat_feed, lv_color_hex(0x162339), 0);
    lv_obj_set_scroll_dir(ui->chat_feed, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui->chat_feed, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_layout(ui->chat_feed, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui->chat_feed, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui->chat_feed, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(ui->chat_feed, LV_OBJ_FLAG_GESTURE_BUBBLE);

    center_actions = lv_obj_create(center);
    lv_obj_remove_style_all(center_actions);
    lv_obj_set_size(center_actions, LV_PCT(100), CHAT_ACTION_BTN_H);
    lv_obj_set_layout(center_actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(center_actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(center_actions, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(center_actions, 4, 0);

    action_group = lv_obj_create(center_actions);
    lv_obj_remove_style_all(action_group);
    lv_obj_set_size(action_group, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_set_layout(action_group, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(action_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(action_group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(action_group, 4, 0);

    create_action_button(ui, action_group, "CLEAR CHAT", lv_color_hex(0xff7f50), chat_clear_event_cb, NULL);
    create_action_button(ui, action_group, "TAIL ON", lv_color_hex(0x63e6be), chat_follow_event_cb, &ui->chat_follow_label);
    create_action_button(ui, action_group, "CLEAR ERR", lv_color_hex(0xffb347), chat_clear_error_event_cb, NULL);

    ui->chat_hint_value = lv_label_create(center_actions);
    lv_obj_set_width(ui->chat_hint_value, 180);
    lv_obj_set_flex_grow(ui->chat_hint_value, 1);
    lv_label_set_long_mode(ui->chat_hint_value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(ui->chat_hint_value, ui->font_pixel_small, 0);
    lv_obj_set_style_text_color(ui->chat_hint_value, lv_color_hex(0x9fb8c8), 0);
    lv_obj_set_style_text_align(ui->chat_hint_value, LV_TEXT_ALIGN_RIGHT, 0);

    ui->chat_error_value = lv_label_create(center);
    lv_obj_set_width(ui->chat_error_value, LV_PCT(100));
    lv_obj_set_height(ui->chat_error_value, CHAT_ERROR_TEXT_H);
    lv_label_set_long_mode(ui->chat_error_value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(ui->chat_error_value, ui->font_pixel_small, 0);
    lv_obj_set_style_text_color(ui->chat_error_value, lv_color_hex(0xff6b6b), 0);
    lv_obj_set_style_text_align(ui->chat_error_value, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_add_flag(ui->chat_error_value, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *right = create_panel(parent, &ui->panel_style, CHAT_PANEL_RIGHT_W);
    compact_panel(right, CHAT_PANEL_PAD, CHAT_PANEL_GAP);
    lv_obj_set_layout(right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(right, 8, 0);

    lv_obj_t *right_header = lv_obj_create(right);
    lv_obj_remove_style_all(right_header);
    lv_obj_set_width(right_header, LV_PCT(100));
    lv_obj_set_height(right_header, CHAT_HEADER_HEIGHT);
    lv_obj_set_style_pad_all(right_header, 0, 0);
    lv_obj_clear_flag(right_header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *right_title = lv_label_create(right_header);
    lv_obj_set_style_text_font(right_title, ui->font_pixel_small, 0);
    lv_obj_set_style_text_color(right_title, lv_color_hex(0x63e6be), 0);
    lv_label_set_text(right_title, "STATE");
    lv_obj_align(right_title, LV_ALIGN_LEFT_MID, 0, 0);

    state_grid = lv_obj_create(right);
    lv_obj_remove_style_all(state_grid);
    lv_obj_set_width(state_grid, LV_PCT(100));
    lv_obj_set_flex_grow(state_grid, 1);
    lv_obj_set_layout(state_grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(state_grid, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state_grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(state_grid, 8, 0);
    lv_obj_clear_flag(state_grid, LV_OBJ_FLAG_SCROLLABLE);

    create_state_tile(ui, state_grid, "VOICE", lv_color_hex(0x63e6be),
                      UI_FS_SRC(T113CLAW_UI_ICON_VOICE_IDLE),
                      &ui->chat_voice_icon, &ui->chat_voice_state);
    create_state_tile(ui, state_grid, "AGENT", lv_color_hex(0xffb347),
                      UI_FS_SRC(T113CLAW_UI_ICON_AGENT_READY),
                      &ui->chat_agent_icon, &ui->chat_agent_state);

    page_chat_set_voice_state(ui, ui->voice_state[0] ? ui->voice_state : "待命");
    page_chat_set_agent_state(ui, ui->agent_state[0] ? ui->agent_state : "READY");
    chat_refresh_runtime_error(ui);
    chat_refresh_overview(ui);
    chat_rebuild_feed(ui);
}

void page_chat_append(ui_context_t *ui, const char *role, const char *channel, const char *text)
{
    ui_chat_role_t chat_role = UI_CHAT_ROLE_SYSTEM;
    char sanitized_text[UI_TEXT_MAX];
    const char *display_text = text ? text : "";

    if (role && strcmp(role, "user") == 0) {
        chat_role = UI_CHAT_ROLE_USER;
    } else if (role && strcmp(role, "assistant") == 0) {
        chat_role = UI_CHAT_ROLE_ASSISTANT;
    }

    if (chat_role == UI_CHAT_ROLE_ASSISTANT) {
        chat_sanitize_assistant_text(display_text, sanitized_text, sizeof(sanitized_text));
        if (sanitized_text[0] != '\0') {
            display_text = sanitized_text;
        }
    }

    chat_push_entry(ui,
                    chat_role,
                    channel,
                    display_text,
                    (chat_role == UI_CHAT_ROLE_USER || chat_role == UI_CHAT_ROLE_ASSISTANT) && channel && channel[0]);
}

void page_chat_set_voice_state(ui_context_t *ui, const char *state)
{
    const char *next_state = state ? state : "待命";

    snprintf(ui->voice_state, sizeof(ui->voice_state), "%s", next_state);
    if (ui->chat_voice_state) {
        lv_label_set_text(ui->chat_voice_state, ui->voice_state);
    }
    ui_set_png_image_src(ui->chat_voice_icon, ui_voice_icon_src(ui->voice_state), CHAT_STATE_ICON_ZOOM);
    if (!chat_state_has_error(ui->voice_state)) {
        page_chat_clear_runtime_error(ui, "voice");
    }
    chat_refresh_overview(ui);
}

void page_chat_set_agent_state(ui_context_t *ui, const char *state)
{
    const char *next_state = state ? state : "READY";

    snprintf(ui->agent_state, sizeof(ui->agent_state), "%s", next_state);
    if (ui->chat_agent_state) {
        lv_label_set_text(ui->chat_agent_state, ui->agent_state);
    }
    ui_set_png_image_src(ui->chat_agent_icon, ui_agent_icon_src(ui->agent_state), CHAT_STATE_ICON_ZOOM);
    if (!chat_state_has_error(ui->agent_state)) {
        page_chat_clear_runtime_error(ui, "agent");
    }

    chat_refresh_overview(ui);
}

void page_chat_set_runtime_error(ui_context_t *ui, const char *source, const char *text)
{
    char formatted[160];
    const char *error_source;

    if (!ui || !text || text[0] == '\0') return;

    error_source = (source && source[0] != '\0') ? source : "runtime";
    snprintf(formatted, sizeof(formatted), "ERROR %s: %s", error_source, text);

    if (strcmp(ui->chat_error_source, error_source) == 0 &&
        strcmp(ui->chat_error_text, formatted) == 0) {
        return;
    }

    snprintf(ui->chat_error_source, sizeof(ui->chat_error_source), "%s", error_source);
    snprintf(ui->chat_error_text, sizeof(ui->chat_error_text), "%s", formatted);
    chat_refresh_runtime_error(ui);
}

void page_chat_clear_runtime_error(ui_context_t *ui, const char *source)
{
    if (!ui) return;
    if (ui->chat_error_text[0] == '\0') return;
    if (source && !chat_error_source_matches(ui->chat_error_source, source)) return;

    ui->chat_error_source[0] = '\0';
    ui->chat_error_text[0] = '\0';
    chat_refresh_runtime_error(ui);
}

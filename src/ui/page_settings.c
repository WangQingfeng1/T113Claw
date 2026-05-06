#include "ui_private.h"
#include "ui_manager.h"
#include "config/config.h"
#include "t113claw_config.h"
#include "services/audio_service.h"
#include "services/wifi_service.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    SETTINGS_VIEW_WIFI = 0,
    SETTINGS_VIEW_SERVER,
    SETTINGS_VIEW_SYSTEM,
} settings_view_t;

typedef struct {
    char ssid[96];
    char pass[96];
} wifi_apply_task_t;

typedef struct {
    char text[160];
    lv_color_t color;
    bool busy;
} wifi_status_update_t;

typedef struct {
    ui_context_t *ui;
    settings_view_t active_view;
    lv_obj_t *wifi_tab_btn;
    lv_obj_t *wifi_tab_label;
    lv_obj_t *server_tab_btn;
    lv_obj_t *server_tab_label;
    lv_obj_t *system_tab_btn;
    lv_obj_t *system_tab_label;
    lv_obj_t *wifi_panel;
    lv_obj_t *server_panel;
    lv_obj_t *system_panel;
    lv_obj_t *wifi_state_value;
    lv_obj_t *wifi_hint_label;
    lv_obj_t *wifi_ssid_ta;
    lv_obj_t *wifi_pass_ta;
    lv_obj_t *wifi_apply_btn;
    lv_obj_t *wifi_apply_label;
    lv_obj_t *wifi_keyboard;
    lv_obj_t *server_state_value;
    lv_obj_t *server_hint_label;
    lv_obj_t *server_host_ta;
    lv_obj_t *server_port_ta;
    lv_obj_t *server_user_ta;
    lv_obj_t *server_pass_ta;
    lv_obj_t *server_apply_btn;
    lv_obj_t *server_apply_label;
    lv_obj_t *server_keyboard;
    lv_obj_t *system_volume_slider;
    lv_obj_t *system_volume_value;
    bool wifi_apply_busy;
    bool server_apply_busy;
} settings_page_state_t;

static settings_page_state_t s_settings;

static int settings_safe_volume(int volume_pct)
{
    if (volume_pct < 60) return 60;
    if (volume_pct > 80) return 80;
    return volume_pct;
}

static int settings_safe_display_volume(int volume_pct)
{
    if (volume_pct < 0) return 0;
    if (volume_pct > 100) return 100;
    return volume_pct;
}

static int settings_volume_actual_to_display(int actual_pct)
{
    actual_pct = settings_safe_volume(actual_pct);
    return ((actual_pct - 60) * 100 + 10) / 20;
}

static int settings_volume_display_to_actual(int display_pct)
{
    display_pct = settings_safe_display_volume(display_pct);
    return settings_safe_volume(60 + (display_pct * 20 + 50) / 100);
}

static const char *wifi_state_text(mc_wifi_state_t state)
{
    switch (state) {
    case MC_WIFI_OFF: return "OFFLINE";
    case MC_WIFI_CONNECTING: return "CONNECTING";
    case MC_WIFI_CONNECTED: return "CONNECTED";
    case MC_WIFI_FAILED: return "FAILED";
    }

    return "UNKNOWN";
}

static const char *server_link_state_text(ui_server_link_state_t state)
{
    switch (state) {
    case UI_SERVER_LINK_UNCONFIGURED: return "NOT SET";
    case UI_SERVER_LINK_CONNECTING: return "PROBING";
    case UI_SERVER_LINK_ONLINE: return "ONLINE";
    case UI_SERVER_LINK_FAILED: return "OFFLINE";
    }

    return "UNKNOWN";
}

static lv_color_t server_link_state_color(ui_server_link_state_t state)
{
    switch (state) {
    case UI_SERVER_LINK_UNCONFIGURED:
        return lv_color_hex(0xdbe8f0);
    case UI_SERVER_LINK_CONNECTING:
        return lv_color_hex(0xf4c95d);
    case UI_SERVER_LINK_ONLINE:
        return lv_color_hex(0x63e6be);
    case UI_SERVER_LINK_FAILED:
    default:
        return lv_color_hex(0xff7f50);
    }
}

static const char *server_link_hint_text(ui_context_t *ui)
{
    if (!ui) {
        return "填写配置后点 APPLY";
    }

    switch (ui->server_link_state) {
    case UI_SERVER_LINK_UNCONFIGURED:
        return "填写配置后点 APPLY";
    case UI_SERVER_LINK_CONNECTING:
        return "正在探测服务器";
    case UI_SERVER_LINK_ONLINE:
        return "已连接，可供 Agent 调用";
    case UI_SERVER_LINK_FAILED:
    default:
        return ui->server_link_text[0] ? ui->server_link_text : "连接失败，请检查配置";
    }
}

static void settings_style_mode_btn(lv_obj_t *btn, lv_obj_t *label, bool active, lv_color_t accent)
{
    if (!btn || !label) return;

    lv_obj_set_style_bg_color(btn, active ? accent : lv_color_hex(0x182539), 0);
    lv_obj_set_style_border_color(btn, active ? accent : lv_color_hex(0x58d7c6), 0);
    lv_obj_set_style_text_color(label, active ? lv_color_hex(0x08111b) : lv_color_hex(0xd8f5ff), 0);
}

static void settings_update_mode_visuals(void)
{
    settings_style_mode_btn(s_settings.wifi_tab_btn, s_settings.wifi_tab_label,
                            s_settings.active_view == SETTINGS_VIEW_WIFI,
                            lv_color_hex(0x4cc9f0));
    settings_style_mode_btn(s_settings.server_tab_btn, s_settings.server_tab_label,
                            s_settings.active_view == SETTINGS_VIEW_SERVER,
                            lv_color_hex(0xf4c95d));
    settings_style_mode_btn(s_settings.system_tab_btn, s_settings.system_tab_label,
                            s_settings.active_view == SETTINGS_VIEW_SYSTEM,
                            lv_color_hex(0x63e6be));
}

static void settings_switch_view(settings_view_t view)
{
    s_settings.active_view = view;

    if (s_settings.wifi_panel) {
        if (view == SETTINGS_VIEW_WIFI) {
            lv_obj_clear_flag(s_settings.wifi_panel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_settings.wifi_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_settings.system_panel) {
        if (view == SETTINGS_VIEW_SYSTEM) {
            lv_obj_clear_flag(s_settings.system_panel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_settings.system_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_settings.server_panel) {
        if (view == SETTINGS_VIEW_SERVER) {
            lv_obj_clear_flag(s_settings.server_panel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_settings.server_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }

    settings_update_mode_visuals();
}

static void settings_focus_targets(lv_obj_t *ta,
                                   lv_obj_t **targets,
                                   size_t count,
                                   lv_obj_t *keyboard)
{
    for (size_t i = 0; i < count; ++i) {
        if (!targets[i]) continue;
        if (targets[i] == ta) {
            lv_obj_add_state(targets[i], LV_STATE_FOCUSED);
            lv_textarea_set_cursor_pos(targets[i], LV_TEXTAREA_CURSOR_LAST);
        } else {
            lv_obj_clear_state(targets[i], LV_STATE_FOCUSED);
        }
        lv_obj_invalidate(targets[i]);
    }

    if (keyboard && ta) {
        lv_keyboard_set_textarea(keyboard, ta);
    }
}

static void settings_focus_wifi_textarea(lv_obj_t *ta)
{
    lv_obj_t *targets[] = {s_settings.wifi_ssid_ta, s_settings.wifi_pass_ta};
    settings_focus_targets(ta, targets, sizeof(targets) / sizeof(targets[0]),
                           s_settings.wifi_keyboard);
}

static void settings_focus_server_textarea(lv_obj_t *ta)
{
    lv_obj_t *targets[] = {
        s_settings.server_host_ta,
        s_settings.server_user_ta,
        s_settings.server_port_ta,
        s_settings.server_pass_ta,
    };
    settings_focus_targets(ta, targets, sizeof(targets) / sizeof(targets[0]),
                           s_settings.server_keyboard);
}

static lv_obj_t *create_title_btn(ui_context_t *ui, lv_obj_t *parent,
                                  const char *icon_src, uint16_t zoom,
                                  const char *text, lv_coord_t width,
                                  lv_obj_t **label_out)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, width, 32);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x182539), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x58d7c6), 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_pad_left(btn, 10, 0);
    lv_obj_set_style_pad_right(btn, 10, 0);
    lv_obj_set_style_pad_top(btn, 0, 0);
    lv_obj_set_style_pad_bottom(btn, 0, 0);
    lv_obj_set_layout(btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(btn, 8, 0);

    ui_create_png_image(btn, icon_src, zoom);

    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, ui->font_pixel_small, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xd8f5ff), 0);
    lv_label_set_text(label, text);

    if (label_out) *label_out = label;
    return btn;
}

static lv_obj_t *create_input_block(ui_context_t *ui, lv_obj_t *parent,
                                    const char *title, const char *placeholder,
                                    bool password, lv_coord_t width)
{
    lv_obj_t *block = lv_obj_create(parent);
    lv_obj_set_size(block, width, 44);
    lv_obj_add_style(block, &ui->card_style, 0);
    lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_label = lv_label_create(block);
    lv_obj_set_style_text_font(title_label, ui->font_pixel_small, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xf4c95d), 0);
    lv_label_set_text(title_label, title);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, -2);

    lv_obj_t *ta = lv_textarea_create(block);
    lv_obj_set_size(ta, LV_PCT(100), 22);
    lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x101826), 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(0x58d7c6), 0);
    lv_obj_set_style_border_width(ta, 2, 0);
    lv_obj_set_style_radius(ta, 0, 0);
    lv_obj_set_style_pad_left(ta, 6, 0);
    lv_obj_set_style_pad_right(ta, 6, 0);
    lv_obj_set_style_pad_top(ta, 2, 0);
    lv_obj_set_style_pad_bottom(ta, 2, 0);
    lv_obj_set_style_text_font(ta, ui->font_cn_small, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(0xf3f7fb), 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(0xf4c95d), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0xf4c95d), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_anim_time(ta, 400, LV_PART_CURSOR);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, password);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_password_show_time(ta, 800);
    lv_textarea_set_cursor_click_pos(ta, true);
    return ta;
}

static lv_obj_t *create_action_button(ui_context_t *ui, lv_obj_t *parent,
                                      const char *label, lv_color_t bg,
                                      lv_coord_t width, lv_obj_t **label_out)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_add_style(btn, &ui->badge_style, 0);
    lv_obj_set_size(btn, width, 34);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xe8f0f7), 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_clear_state(btn, LV_STATE_FOCUS_KEY);

    lv_obj_t *text = lv_label_create(btn);
    lv_obj_set_style_text_font(text, ui->font_pixel_small, 0);
    lv_obj_set_style_text_color(text, lv_color_hex(0x08111b), 0);
    lv_label_set_text(text, label);
    lv_obj_center(text);

    if (label_out) *label_out = text;
    return btn;
}

static void settings_apply_wifi_status_now(const char *text, lv_color_t color, bool busy)
{
    s_settings.wifi_apply_busy = busy;

    if (s_settings.wifi_hint_label) {
        lv_label_set_text(s_settings.wifi_hint_label, text ? text : "");
        lv_obj_set_style_text_color(s_settings.wifi_hint_label, color, 0);
    }

    if (s_settings.wifi_apply_label) {
        lv_label_set_text(s_settings.wifi_apply_label, busy ? "APPLYING" : "SAVE+RECONNECT");
    }

    if (s_settings.wifi_apply_btn) {
        if (busy) {
            lv_obj_add_state(s_settings.wifi_apply_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(s_settings.wifi_apply_btn, LV_STATE_DISABLED);
        }
    }
}

static void settings_apply_wifi_status_async(void *data)
{
    wifi_status_update_t *update = (wifi_status_update_t *)data;
    if (!update) return;

    settings_apply_wifi_status_now(update->text, update->color, update->busy);
    free(update);
}

static void settings_queue_wifi_status(const char *text, lv_color_t color, bool busy)
{
    wifi_status_update_t *update = calloc(1, sizeof(*update));
    if (!update) return;

    if (text) {
        snprintf(update->text, sizeof(update->text), "%s", text);
    }
    update->color = color;
    update->busy = busy;
    lv_async_call(settings_apply_wifi_status_async, update);
}

static void *settings_wifi_apply_thread(void *arg)
{
    wifi_apply_task_t *task = (wifi_apply_task_t *)arg;
    if (!task) return NULL;

    settings_queue_wifi_status("保存并重连中", lv_color_hex(0xf4c95d), true);

    if (config_set("wifi", "ssid", task->ssid) != 0 ||
        config_set("wifi", "pass", task->pass) != 0 ||
        config_save(T113CLAW_CONFIG_FILE) != 0) {
        settings_queue_wifi_status("配置保存失败", lv_color_hex(0xff7f50), false);
        free(task);
        return NULL;
    }

    wifi_service_stop();
    if (wifi_service_start() == 0) {
        settings_queue_wifi_status("已保存并重连", lv_color_hex(0x63e6be), false);
    } else {
        settings_queue_wifi_status("已保存, 连接失败", lv_color_hex(0xff7f50), false);
    }

    free(task);
    return NULL;
}

static void settings_back_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_CLICKED) return;

    if (s_settings.ui) {
        ui_page_show(s_settings.ui, UI_PAGE_CHAT);
    }
    lv_event_stop_bubbling(event);
}

static void settings_mode_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_CLICKED) return;

    settings_switch_view((settings_view_t)(uintptr_t)lv_event_get_user_data(event));
    lv_event_stop_bubbling(event);
}

static void settings_textarea_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_FOCUSED) return;

    settings_focus_wifi_textarea(lv_event_get_target(event));
}

static void settings_server_textarea_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_FOCUSED) return;

    settings_focus_server_textarea(lv_event_get_target(event));
}

static void settings_wifi_apply_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_CLICKED) return;
    if (s_settings.wifi_apply_busy) return;

    const char *ssid = lv_textarea_get_text(s_settings.wifi_ssid_ta);
    const char *pass = lv_textarea_get_text(s_settings.wifi_pass_ta);
    if (!ssid || ssid[0] == '\0') {
        settings_apply_wifi_status_now("SSID 不能为空", lv_color_hex(0xff7f50), false);
        return;
    }

    wifi_apply_task_t *task = calloc(1, sizeof(*task));
    if (!task) {
        settings_apply_wifi_status_now("WiFi 任务不足", lv_color_hex(0xff7f50), false);
        return;
    }

    snprintf(task->ssid, sizeof(task->ssid), "%s", ssid);
    snprintf(task->pass, sizeof(task->pass), "%s", pass ? pass : "");

    settings_apply_wifi_status_now("准备应用配置", lv_color_hex(0xf4c95d), true);

    {
        pthread_t worker;
        if (pthread_create(&worker, NULL, settings_wifi_apply_thread, task) != 0) {
            free(task);
            settings_apply_wifi_status_now("任务启动失败", lv_color_hex(0xff7f50), false);
            return;
        }
        pthread_detach(worker);
    }

    lv_event_stop_bubbling(event);
}

static void settings_server_apply_status(const char *text, lv_color_t color)
{
    bool busy = s_settings.ui && s_settings.ui->server_link_state == UI_SERVER_LINK_CONNECTING;
    s_settings.server_apply_busy = busy;

    if (s_settings.server_hint_label) {
        lv_label_set_text(s_settings.server_hint_label, text ? text : "");
        lv_obj_set_style_text_color(s_settings.server_hint_label, color, 0);
    }

    if (s_settings.server_apply_label) {
        lv_label_set_text(s_settings.server_apply_label, busy ? "APPLYING" : "APPLY");
    }

    if (s_settings.server_apply_btn) {
        if (busy) {
            lv_obj_add_state(s_settings.server_apply_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(s_settings.server_apply_btn, LV_STATE_DISABLED);
        }
    }
}

static void settings_server_apply_event_cb(lv_event_t *event)
{
    const char *host;
    const char *port;
    const char *username;
    const char *password;
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_CLICKED) return;
    if (s_settings.server_apply_busy) return;

    host = lv_textarea_get_text(s_settings.server_host_ta);
    port = lv_textarea_get_text(s_settings.server_port_ta);
    username = lv_textarea_get_text(s_settings.server_user_ta);
    password = lv_textarea_get_text(s_settings.server_pass_ta);

    if (!host || host[0] == '\0') {
        settings_server_apply_status("HOST 不能为空", lv_color_hex(0xff7f50));
        return;
    }
    if (!username || username[0] == '\0') {
        settings_server_apply_status("USER 不能为空", lv_color_hex(0xff7f50));
        return;
    }
    if (!password || password[0] == '\0') {
        settings_server_apply_status("PASSWORD 不能为空", lv_color_hex(0xff7f50));
        return;
    }

    config_set("remote", "host", host);
    config_set("remote", "port", (port && port[0]) ? port : T113CLAW_REMOTE_PORT_DEFAULT);
    config_set("remote", "username", username);
    config_set("remote", "password", password);

    if (config_save(T113CLAW_CONFIG_FILE) != 0) {
        settings_server_apply_status("写入 config.ini 失败", lv_color_hex(0xff7f50));
        return;
    }

    if (s_settings.ui) {
        s_settings.ui->server_link_state = UI_SERVER_LINK_CONNECTING;
        snprintf(s_settings.ui->server_link_text,
                 sizeof(s_settings.ui->server_link_text),
                 "%s", "已保存，正在探测");
    }
    settings_server_apply_status("已保存，正在探测", lv_color_hex(0x63e6be));
    ui_manager_request_server_probe();
    page_settings_refresh(s_settings.ui);
    lv_event_stop_bubbling(event);
}

static void settings_volume_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *slider = lv_event_get_target(event);
    int display_pct = lv_slider_get_value(slider);
    int actual_pct = settings_volume_display_to_actual(display_pct);
    char buf[16];

    snprintf(buf, sizeof(buf), "%d%%", display_pct);
    if (s_settings.system_volume_value) {
        lv_label_set_text(s_settings.system_volume_value, buf);
    }

    if (code != LV_EVENT_RELEASED) return;

    snprintf(buf, sizeof(buf), "%d", actual_pct);
    config_set("system", "volume", buf);
    config_save(T113CLAW_CONFIG_FILE);

#ifndef SIMULATOR_LINUX
    audio_service_set_volume(actual_pct);
#endif
}

static void settings_reboot_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_CLICKED) return;

#ifndef SIMULATOR_LINUX
    config_save(T113CLAW_CONFIG_FILE);
    system("sync");
    system("reboot");
#endif

    lv_event_stop_bubbling(event);
}

static void build_wifi_view(ui_context_t *ui, lv_obj_t *parent)
{
    s_settings.wifi_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(s_settings.wifi_panel);
    lv_obj_set_size(s_settings.wifi_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(s_settings.wifi_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_settings.wifi_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(s_settings.wifi_panel, 10, 0);
    lv_obj_clear_flag(s_settings.wifi_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left = lv_obj_create(s_settings.wifi_panel);
    lv_obj_set_size(left, 480, LV_PCT(100));
    lv_obj_add_style(left, &ui->panel_style, 0);
    lv_obj_set_layout(left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(left, 6, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *state_row = lv_obj_create(left);
    lv_obj_set_size(state_row, LV_PCT(100), 28);
    lv_obj_add_style(state_row, &ui->badge_style, 0);
    lv_obj_clear_flag(state_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *state_title = lv_label_create(state_row);
    lv_obj_set_style_text_font(state_title, ui->font_pixel_small, 0);
    lv_obj_set_style_text_color(state_title, lv_color_hex(0xf4c95d), 0);
    lv_label_set_text(state_title, "WIFI STATE");
    lv_obj_align(state_title, LV_ALIGN_LEFT_MID, 0, 0);

    s_settings.wifi_state_value = lv_label_create(state_row);
    lv_obj_set_style_text_font(s_settings.wifi_state_value, ui->font_pixel_small, 0);
    lv_obj_set_style_text_color(s_settings.wifi_state_value, lv_color_hex(0x63e6be), 0);
    lv_label_set_text(s_settings.wifi_state_value, "CONNECTED");
    lv_obj_align(s_settings.wifi_state_value, LV_ALIGN_RIGHT_MID, 0, 0);

    s_settings.wifi_ssid_ta = create_input_block(ui, left, "SSID", "例如: T113Claw-2.4G", false, LV_PCT(100));
    s_settings.wifi_pass_ta = create_input_block(ui, left, "PASSWORD", "输入 WiFi 密码", true, LV_PCT(100));
    lv_obj_add_event_cb(s_settings.wifi_ssid_ta, settings_textarea_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(s_settings.wifi_pass_ta, settings_textarea_event_cb, LV_EVENT_ALL, NULL);

    lv_textarea_set_text(s_settings.wifi_ssid_ta, config_get_default("wifi", "ssid", ""));
    lv_textarea_set_text(s_settings.wifi_pass_ta, config_get_default("wifi", "pass", ""));

    lv_obj_t *apply_row = lv_obj_create(left);
    lv_obj_remove_style_all(apply_row);
    lv_obj_set_size(apply_row, LV_PCT(100), 40);
    lv_obj_set_layout(apply_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(apply_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(apply_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(apply_row, 10, 0);
    lv_obj_clear_flag(apply_row, LV_OBJ_FLAG_SCROLLABLE);

    s_settings.wifi_apply_btn = create_action_button(ui, apply_row, "SAVE+RECONNECT",
                                                     lv_color_hex(0x63e6be), 190,
                                                     &s_settings.wifi_apply_label);
    lv_obj_add_event_cb(s_settings.wifi_apply_btn, settings_wifi_apply_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_settings.wifi_apply_btn, settings_wifi_apply_event_cb, LV_EVENT_CLICKED, NULL);

    s_settings.wifi_hint_label = lv_label_create(apply_row);
    lv_obj_set_width(s_settings.wifi_hint_label, 250);
    lv_obj_set_style_text_font(s_settings.wifi_hint_label, ui->font_cn_small, 0);
    lv_obj_set_style_text_color(s_settings.wifi_hint_label, lv_color_hex(0xdbe8f0), 0);
    lv_label_set_long_mode(s_settings.wifi_hint_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_settings.wifi_hint_label, "填写完毕后保存并重连。");

    lv_obj_t *right = lv_obj_create(s_settings.wifi_panel);
    lv_obj_add_style(right, &ui->panel_alt_style, 0);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_layout(right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(right, 4, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *keyboard_title = lv_obj_create(right);
    lv_obj_set_size(keyboard_title, LV_PCT(100), 20);
    lv_obj_add_style(keyboard_title, &ui->badge_style, 0);
    lv_obj_clear_flag(keyboard_title, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *keyboard_label = lv_label_create(keyboard_title);
    lv_obj_set_style_text_font(keyboard_label, ui->font_pixel_small, 0);
    lv_obj_set_style_text_color(keyboard_label, lv_color_hex(0xf4c95d), 0);
    lv_label_set_text(keyboard_label, "KEYBOARD");
    lv_obj_align(keyboard_label, LV_ALIGN_LEFT_MID, 0, 0);

    s_settings.wifi_keyboard = lv_keyboard_create(right);
    lv_obj_set_size(s_settings.wifi_keyboard, LV_PCT(100), 152);
    lv_obj_set_style_bg_color(s_settings.wifi_keyboard, lv_color_hex(0x0f1927), 0);
    lv_obj_set_style_bg_opa(s_settings.wifi_keyboard, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(s_settings.wifi_keyboard, lv_color_hex(0xffffff), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_settings.wifi_keyboard, LV_OPA_30, LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_settings.wifi_keyboard, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_settings.wifi_keyboard, lv_color_hex(0x0a0f18), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_settings.wifi_keyboard, lv_color_hex(0xf4c95d), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(s_settings.wifi_keyboard, LV_OPA_40, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(s_settings.wifi_keyboard, &lv_font_montserrat_20, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(s_settings.wifi_keyboard, lv_color_hex(0x08111b), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_radius(s_settings.wifi_keyboard, 8, LV_PART_ITEMS);
    lv_keyboard_set_popovers(s_settings.wifi_keyboard, true);
    lv_keyboard_set_mode(s_settings.wifi_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    settings_focus_wifi_textarea(s_settings.wifi_ssid_ta);
}

static void build_server_view(ui_context_t *ui, lv_obj_t *parent)
{
    s_settings.server_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(s_settings.server_panel);
    lv_obj_set_size(s_settings.server_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(s_settings.server_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_settings.server_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(s_settings.server_panel, 10, 0);
    lv_obj_clear_flag(s_settings.server_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left = lv_obj_create(s_settings.server_panel);
    lv_obj_set_size(left, 480, LV_PCT(100));
    lv_obj_add_style(left, &ui->panel_style, 0);
    lv_obj_set_layout(left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(left, 6, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *state_row = lv_obj_create(left);
    lv_obj_set_size(state_row, LV_PCT(100), 28);
    lv_obj_add_style(state_row, &ui->badge_style, 0);
    lv_obj_clear_flag(state_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *state_title = lv_label_create(state_row);
    lv_obj_set_style_text_font(state_title, ui->font_pixel_small, 0);
    lv_obj_set_style_text_color(state_title, lv_color_hex(0xf4c95d), 0);
    lv_label_set_text(state_title, "SERVER STATE");
    lv_obj_align(state_title, LV_ALIGN_LEFT_MID, 0, 0);

    s_settings.server_state_value = lv_label_create(state_row);
    lv_obj_set_style_text_font(s_settings.server_state_value, ui->font_pixel_small, 0);
    lv_obj_set_style_text_color(s_settings.server_state_value, lv_color_hex(0xdbe8f0), 0);
    lv_label_set_text(s_settings.server_state_value, "NOT SET");
    lv_obj_align(s_settings.server_state_value, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *host_row = lv_obj_create(left);
    lv_obj_remove_style_all(host_row);
    lv_obj_set_size(host_row, LV_PCT(100), 44);
    lv_obj_set_layout(host_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(host_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(host_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(host_row, 10, 0);
    lv_obj_clear_flag(host_row, LV_OBJ_FLAG_SCROLLABLE);

    s_settings.server_host_ta = create_input_block(ui, host_row, "HOST", "例如: 192.168.1.8", false, 220);
    s_settings.server_user_ta = create_input_block(ui, host_row, "USER", "输入用户名", false, 220);
    lv_obj_add_event_cb(s_settings.server_host_ta, settings_server_textarea_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(s_settings.server_user_ta, settings_server_textarea_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *auth_row = lv_obj_create(left);
    lv_obj_remove_style_all(auth_row);
    lv_obj_set_size(auth_row, LV_PCT(100), 44);
    lv_obj_set_layout(auth_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(auth_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(auth_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(auth_row, 10, 0);
    lv_obj_clear_flag(auth_row, LV_OBJ_FLAG_SCROLLABLE);

    s_settings.server_port_ta = create_input_block(ui, auth_row, "PORT", "默认8765", false, 110);
    s_settings.server_pass_ta = create_input_block(ui, auth_row, "PASSWORD", "输入服务器密码", true, 330);
    lv_obj_add_event_cb(s_settings.server_port_ta, settings_server_textarea_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(s_settings.server_pass_ta, settings_server_textarea_event_cb, LV_EVENT_ALL, NULL);

    lv_textarea_set_text(s_settings.server_host_ta, config_get_default("remote", "host", ""));
    lv_textarea_set_text(s_settings.server_port_ta, config_get_default("remote", "port", T113CLAW_REMOTE_PORT_DEFAULT));
    lv_textarea_set_text(s_settings.server_user_ta, config_get_default("remote", "username", ""));
    lv_textarea_set_text(s_settings.server_pass_ta, config_get_default("remote", "password", ""));

    lv_obj_t *apply_row = lv_obj_create(left);
    lv_obj_remove_style_all(apply_row);
    lv_obj_set_size(apply_row, LV_PCT(100), 40);
    lv_obj_set_layout(apply_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(apply_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(apply_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(apply_row, 10, 0);
    lv_obj_clear_flag(apply_row, LV_OBJ_FLAG_SCROLLABLE);

    s_settings.server_apply_btn = create_action_button(ui, apply_row, "APPLY",
                                                       lv_color_hex(0xf4c95d), 190,
                                                       &s_settings.server_apply_label);
    lv_obj_add_event_cb(s_settings.server_apply_btn, settings_server_apply_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_settings.server_apply_btn, settings_server_apply_event_cb, LV_EVENT_CLICKED, NULL);

    s_settings.server_hint_label = lv_label_create(apply_row);
    lv_obj_set_width(s_settings.server_hint_label, 250);
    lv_obj_set_style_text_font(s_settings.server_hint_label, ui->font_cn_small, 0);
    lv_obj_set_style_text_color(s_settings.server_hint_label, lv_color_hex(0xdbe8f0), 0);
    lv_label_set_long_mode(s_settings.server_hint_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_settings.server_hint_label, "填写配置后点 APPLY。");

    lv_obj_t *right = lv_obj_create(s_settings.server_panel);
    lv_obj_add_style(right, &ui->panel_alt_style, 0);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_layout(right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(right, 4, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *keyboard_title = lv_obj_create(right);
    lv_obj_set_size(keyboard_title, LV_PCT(100), 20);
    lv_obj_add_style(keyboard_title, &ui->badge_style, 0);
    lv_obj_clear_flag(keyboard_title, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *keyboard_label = lv_label_create(keyboard_title);
    lv_obj_set_style_text_font(keyboard_label, ui->font_pixel_small, 0);
    lv_obj_set_style_text_color(keyboard_label, lv_color_hex(0xf4c95d), 0);
    lv_label_set_text(keyboard_label, "KEYBOARD");
    lv_obj_align(keyboard_label, LV_ALIGN_LEFT_MID, 0, 0);

    s_settings.server_keyboard = lv_keyboard_create(right);
    lv_obj_set_size(s_settings.server_keyboard, LV_PCT(100), 152);
    lv_obj_set_style_bg_color(s_settings.server_keyboard, lv_color_hex(0x0f1927), 0);
    lv_obj_set_style_bg_opa(s_settings.server_keyboard, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(s_settings.server_keyboard, lv_color_hex(0xffffff), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_settings.server_keyboard, LV_OPA_30, LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_settings.server_keyboard, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_settings.server_keyboard, lv_color_hex(0x0a0f18), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_settings.server_keyboard, lv_color_hex(0xf4c95d), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(s_settings.server_keyboard, LV_OPA_40, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(s_settings.server_keyboard, &lv_font_montserrat_20, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(s_settings.server_keyboard, lv_color_hex(0x08111b), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_radius(s_settings.server_keyboard, 8, LV_PART_ITEMS);
    lv_keyboard_set_popovers(s_settings.server_keyboard, true);
    lv_keyboard_set_mode(s_settings.server_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    settings_focus_server_textarea(s_settings.server_host_ta);
}

static void build_system_view(ui_context_t *ui, lv_obj_t *parent)
{
    int actual_volume_pct = settings_safe_volume(atoi(config_get_default("system", "volume", "70")));
    int display_volume_pct = settings_volume_actual_to_display(actual_volume_pct);

    s_settings.system_panel = lv_obj_create(parent);
    lv_obj_set_size(s_settings.system_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_add_style(s_settings.system_panel, &ui->panel_alt_style, 0);
    lv_obj_set_layout(s_settings.system_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_settings.system_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_settings.system_panel, 8, 0);
    lv_obj_clear_flag(s_settings.system_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *volume_card = lv_obj_create(s_settings.system_panel);
    lv_obj_set_size(volume_card, LV_PCT(100), 88);
    lv_obj_add_style(volume_card, &ui->card_style, 0);
    lv_obj_set_style_border_color(volume_card, lv_color_hex(0x63e6be), 0);
    lv_obj_clear_flag(volume_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *volume_row = lv_obj_create(volume_card);
    lv_obj_remove_style_all(volume_row);
    lv_obj_set_size(volume_row, LV_PCT(100), 52);
    lv_obj_set_layout(volume_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(volume_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(volume_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(volume_row, 12, 0);
    lv_obj_align(volume_row, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(volume_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *volume_title = lv_label_create(volume_row);
    lv_obj_set_width(volume_title, 80);
    lv_obj_set_style_text_font(volume_title, ui->font_cn_medium, 0);
    lv_obj_set_style_text_color(volume_title, lv_color_hex(0x63e6be), 0);
    lv_obj_set_style_text_align(volume_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_translate_y(volume_title, -4, 0);
    lv_label_set_text(volume_title, "音量");

    lv_obj_t *volume_icon = ui_create_png_image(volume_row, UI_FS_SRC(T113CLAW_UI_ICON_VOLUME_ON), 192);
    lv_obj_set_style_pad_top(volume_icon, 0, 0);

    s_settings.system_volume_slider = lv_slider_create(volume_row);
    lv_obj_set_size(s_settings.system_volume_slider, 460, 12);
    lv_slider_set_range(s_settings.system_volume_slider, 0, 100);
    lv_slider_set_value(s_settings.system_volume_slider, display_volume_pct, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_settings.system_volume_slider, settings_volume_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_settings.system_volume_slider, settings_volume_event_cb, LV_EVENT_RELEASED, NULL);

    s_settings.system_volume_value = lv_label_create(volume_row);
    lv_obj_set_width(s_settings.system_volume_value, 56);
    lv_obj_set_style_text_font(s_settings.system_volume_value, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_settings.system_volume_value, lv_color_hex(0xf4c95d), 0);
    lv_obj_set_style_translate_y(s_settings.system_volume_value, -2, 0);
    lv_label_set_text_fmt(s_settings.system_volume_value, "%d%%", display_volume_pct);

    lv_obj_t *reboot_card = lv_obj_create(s_settings.system_panel);
    lv_obj_set_size(reboot_card, LV_PCT(100), 60);
    lv_obj_add_style(reboot_card, &ui->card_warn_style, 0);
    lv_obj_clear_flag(reboot_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *reboot_row = lv_obj_create(reboot_card);
    lv_obj_remove_style_all(reboot_row);
    lv_obj_set_size(reboot_row, LV_PCT(100), 40);
    lv_obj_set_layout(reboot_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(reboot_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(reboot_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(reboot_row, 12, 0);
    lv_obj_align(reboot_row, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(reboot_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *reboot_title = lv_label_create(reboot_row);
    lv_obj_set_width(reboot_title, 80);
    lv_obj_set_style_text_font(reboot_title, ui->font_cn_medium, 0);
    lv_obj_set_style_text_color(reboot_title, lv_color_hex(0xf4c95d), 0);
    lv_obj_set_style_text_align(reboot_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_translate_y(reboot_title, -4, 0);
    lv_label_set_text(reboot_title, "重启");

    lv_obj_t *reboot_spacer = lv_obj_create(reboot_row);
    lv_obj_remove_style_all(reboot_spacer);
    lv_obj_set_size(reboot_spacer, 170, 1);
    lv_obj_clear_flag(reboot_spacer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *reboot_btn_label = NULL;
    lv_obj_t *reboot_btn = create_action_button(ui, reboot_row, "REBOOT",
                                                lv_color_hex(0xffb347), 190,
                                                &reboot_btn_label);
    lv_obj_set_height(reboot_btn, 40);
    if (reboot_btn_label) {
        lv_obj_set_style_text_font(reboot_btn_label, ui->font_pixel_large, 0);
    }
    lv_obj_add_event_cb(reboot_btn, settings_reboot_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(reboot_btn, settings_reboot_event_cb, LV_EVENT_CLICKED, NULL);

}

void page_settings_build(ui_context_t *ui, lv_obj_t *parent)
{
    memset(&s_settings, 0, sizeof(s_settings));
    s_settings.ui = ui;

    lv_obj_set_style_bg_img_src(parent, UI_FS_SRC(T113CLAW_UI_BG_SETTINGS), 0);
    lv_obj_set_style_bg_img_opa(parent, LV_OPA_20, 0);
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(parent, 6, 0);

    lv_obj_t *title_bar = lv_obj_create(parent);
    lv_obj_remove_style_all(title_bar);
    lv_obj_set_size(title_bar, LV_PCT(100), 32);
    lv_obj_set_layout(title_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(title_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(title_bar, 8, 0);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back_btn = create_title_btn(ui, title_bar,
                                          UI_FS_SRC(T113CLAW_UI_ICON_BACK), 112,
                                          "BACK", 170, NULL);
    lv_obj_add_event_cb(back_btn, settings_back_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(back_btn, settings_back_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x0f1927), 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0x58d7c6), 0);

    s_settings.wifi_tab_btn = create_title_btn(ui, title_bar,
                                               UI_FS_SRC(T113CLAW_UI_ICON_WIFI_SETTING), 112,
                                               "WIFI", 170, &s_settings.wifi_tab_label);
    lv_obj_add_event_cb(s_settings.wifi_tab_btn, settings_mode_event_cb, LV_EVENT_PRESSED,
                        (void *)(uintptr_t)SETTINGS_VIEW_WIFI);
    lv_obj_add_event_cb(s_settings.wifi_tab_btn, settings_mode_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)SETTINGS_VIEW_WIFI);

    s_settings.server_tab_btn = create_title_btn(ui, title_bar,
                                                 UI_FS_SRC(T113CLAW_UI_ICON_SERVER_SETTING), 112,
                                                 "SERVER LINK", 220, &s_settings.server_tab_label);
    lv_obj_add_event_cb(s_settings.server_tab_btn, settings_mode_event_cb, LV_EVENT_PRESSED,
                        (void *)(uintptr_t)SETTINGS_VIEW_SERVER);
    lv_obj_add_event_cb(s_settings.server_tab_btn, settings_mode_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)SETTINGS_VIEW_SERVER);

    s_settings.system_tab_btn = create_title_btn(ui, title_bar,
                                                 UI_FS_SRC(T113CLAW_UI_ICON_SYSTEM_SETTING), 112,
                                                 "SYSTEM", 170, &s_settings.system_tab_label);
    lv_obj_add_event_cb(s_settings.system_tab_btn, settings_mode_event_cb, LV_EVENT_PRESSED,
                        (void *)(uintptr_t)SETTINGS_VIEW_SYSTEM);
    lv_obj_add_event_cb(s_settings.system_tab_btn, settings_mode_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)SETTINGS_VIEW_SYSTEM);

    lv_obj_t *stage = lv_obj_create(parent);
    lv_obj_remove_style_all(stage);
    lv_obj_set_width(stage, LV_PCT(100));
    lv_obj_set_flex_grow(stage, 1);
    lv_obj_clear_flag(stage, LV_OBJ_FLAG_SCROLLABLE);

    build_wifi_view(ui, stage);
    build_server_view(ui, stage);
    build_system_view(ui, stage);
    settings_switch_view(SETTINGS_VIEW_WIFI);
    page_settings_refresh(ui);
    settings_apply_wifi_status_now("点输入框即可编辑",
                                   lv_color_hex(0xdbe8f0), false);
}

void page_settings_refresh(ui_context_t *ui)
{
    mc_wifi_state_t state = wifi_service_get_state();

    if (s_settings.wifi_state_value) {
        lv_label_set_text(s_settings.wifi_state_value, wifi_state_text(state));
        if (state == MC_WIFI_CONNECTED) {
            lv_obj_set_style_text_color(s_settings.wifi_state_value, lv_color_hex(0x63e6be), 0);
        } else if (state == MC_WIFI_CONNECTING) {
            lv_obj_set_style_text_color(s_settings.wifi_state_value, lv_color_hex(0xf4c95d), 0);
        } else {
            lv_obj_set_style_text_color(s_settings.wifi_state_value, lv_color_hex(0xff7f50), 0);
        }
    }

    if (ui && s_settings.server_state_value) {
        lv_label_set_text(s_settings.server_state_value,
                          server_link_state_text(ui->server_link_state));
        lv_obj_set_style_text_color(s_settings.server_state_value,
                                    server_link_state_color(ui->server_link_state), 0);
    }

    if (ui && s_settings.server_hint_label) {
        settings_server_apply_status(server_link_hint_text(ui),
                                     server_link_state_color(ui->server_link_state));
    }
}
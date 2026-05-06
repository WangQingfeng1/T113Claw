#include "ui_manager.h"
#include "ui_private.h"
#include "t113claw_config.h"
#include "config/config.h"
#include "services/remote_client.h"
#include "services/wifi_service.h"
#include "utils/log.h"
#include "utils/utils.h"

#include "lv_port_disp.h"
#include "lv_port_indev.h"

#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define TAG "ui"
#define UI_HEADER_SETTINGS_ICON_ZOOM 95
#define UI_HEADER_SERVER_ICON_ZOOM   95
#define UI_HEADER_WIFI_ICON_ZOOM 120
#define UI_HEADER_SETTINGS_WRAP_W 30
#define UI_HEADER_SETTINGS_WRAP_H 30
#define UI_HEADER_SERVER_WRAP_W   30
#define UI_HEADER_SERVER_WRAP_H   30
#define UI_HEADER_WIFI_WRAP_W    30
#define UI_HEADER_WIFI_WRAP_H    30
#define UI_SERVER_PROBE_INTERVAL_S 5

typedef enum {
    UI_EVT_USER_MESSAGE = 0,
    UI_EVT_ASSISTANT_MESSAGE,
    UI_EVT_VOICE_STATE,
    UI_EVT_AGENT_STATE,
    UI_EVT_RUNTIME_ERROR,
} ui_async_event_type_t;

typedef struct {
    ui_async_event_type_t type;
    char text[UI_TEXT_MAX];
    char meta[32];
} ui_async_event_t;

typedef struct {
    ui_server_link_state_t state;
    char text[160];
} ui_server_status_update_t;

static ui_context_t s_ui;
static int s_initialized = 0;
static lv_timer_t *s_refresh_timer;
static mc_wifi_state_t s_last_wifi_state = MC_WIFI_OFF;
static bool s_server_probe_busy = false;
static bool s_server_probe_force = false;
static time_t s_last_server_probe_at = 0;

static const char *page_titles[UI_PAGE_COUNT] = {
    "CHAT LOOP",
    "SETTINGS",
};

static void ui_refresh_timer_cb(lv_timer_t *timer);
static void ui_async_apply(void *data);
static void ui_build_shell(ui_context_t *ui);
static const char *ui_wifi_icon_src(mc_wifi_state_t state);
static const char *ui_server_icon_src(ui_server_link_state_t state);
static const char *ui_server_state_text(ui_server_link_state_t state);
static void ui_refresh_header_wifi(ui_context_t *ui);
static void ui_refresh_header_server(ui_context_t *ui);
static void ui_apply_server_status_now(ui_context_t *ui,
                                       ui_server_status_update_t *update);
static void ui_apply_server_status_async(void *data);
static void ui_server_probe_reset_async(void *data);
static void *ui_server_probe_thread(void *arg);
static void ui_maybe_probe_server(ui_context_t *ui);
static void ui_header_settings_event_cb(lv_event_t *event);
static void ui_header_gesture_event_cb(lv_event_t *event);

static void ui_style_init(lv_style_t *style)
{
    lv_style_init(style);
}

lv_obj_t *ui_create_png_image(lv_obj_t *parent, const char *src, uint16_t zoom)
{
    lv_obj_t *image = lv_img_create(parent);
    if (src) {
        lv_img_set_src(image, src);
    }
    if (zoom > 0) {
        lv_img_set_zoom(image, zoom);
    }
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
    return image;
}

void ui_set_png_image_src(lv_obj_t *image, const char *src, uint16_t zoom)
{
    if (!image || !src) return;
    lv_img_set_src(image, src);
    if (zoom > 0) {
        lv_img_set_zoom(image, zoom);
    }
}

const char *ui_voice_icon_src(const char *state)
{
    if (!state || state[0] == '\0') return UI_FS_SRC(T113CLAW_UI_ICON_VOICE_IDLE);
    if (strstr(state, "SPEAKING") || strstr(state, "回答") || strstr(state, "播报")) {
        return UI_FS_SRC(T113CLAW_UI_ICON_VOICE_SPEAKING);
    }
    if (strstr(state, "RECOGNIZING") || strstr(state, "识别")) {
        return UI_FS_SRC(T113CLAW_UI_ICON_VOICE_RECOGNIZING);
    }
    if (strstr(state, "LISTENING") || strstr(state, "聆听")) {
        return UI_FS_SRC(T113CLAW_UI_ICON_VOICE_LISTENING);
    }
    if (strstr(state, "THINKING") || strstr(state, "思考")) {
        return UI_FS_SRC(T113CLAW_UI_ICON_VOICE_ACTIVE);
    }
    if (strstr(state, "WAKE") || strstr(state, "待唤醒") || strstr(state, "待命")) {
        return UI_FS_SRC(T113CLAW_UI_ICON_VOICE_IDLE);
    }
    return UI_FS_SRC(T113CLAW_UI_ICON_VOICE_ACTIVE);
}

const char *ui_agent_icon_src(const char *state)
{
    if (!state || state[0] == '\0') return UI_FS_SRC(T113CLAW_UI_ICON_AGENT_READY);
    if (strstr(state, "ERROR") || strstr(state, "FAILED")) return UI_FS_SRC(T113CLAW_UI_ICON_AGENT_ERROR);
    if (strstr(state, "READY")) return UI_FS_SRC(T113CLAW_UI_ICON_AGENT_READY);
    if (strstr(state, "TOOLS") || strstr(state, "THINK")) return UI_FS_SRC(T113CLAW_UI_ICON_AGENT);
    if (strstr(state, "DONE") || strstr(state, "SUCCESS")) return UI_FS_SRC(T113CLAW_UI_ICON_AGENT_SUCCESS);
    return UI_FS_SRC(T113CLAW_UI_ICON_AGENT);
}

static const char *ui_wifi_icon_src(mc_wifi_state_t state)
{
    switch (state) {
    case MC_WIFI_OFF: return UI_FS_SRC(T113CLAW_UI_ICON_WIFI_OFF);
    case MC_WIFI_CONNECTING: return UI_FS_SRC(T113CLAW_UI_ICON_WIFI_CONNECTING);
    case MC_WIFI_CONNECTED: return UI_FS_SRC(T113CLAW_UI_ICON_WIFI_ON);
    case MC_WIFI_FAILED: return UI_FS_SRC(T113CLAW_UI_ICON_WIFI_FAILED);
    }

    return UI_FS_SRC(T113CLAW_UI_ICON_WIFI_FAILED);
}

static const char *ui_server_icon_src(ui_server_link_state_t state)
{
    switch (state) {
    case UI_SERVER_LINK_UNCONFIGURED:
        return UI_FS_SRC(T113CLAW_UI_ICON_SERVER_OFF);
    case UI_SERVER_LINK_CONNECTING:
        return UI_FS_SRC(T113CLAW_UI_ICON_SERVER_CONNECTING);
    case UI_SERVER_LINK_ONLINE:
        return UI_FS_SRC(T113CLAW_UI_ICON_SERVER_ON);
    case UI_SERVER_LINK_FAILED:
    default:
        return UI_FS_SRC(T113CLAW_UI_ICON_SERVER_FAILED);
    }
}

static const char *ui_server_state_text(ui_server_link_state_t state)
{
    switch (state) {
    case UI_SERVER_LINK_UNCONFIGURED:
        return "NOT SET";
    case UI_SERVER_LINK_CONNECTING:
        return "PROBING";
    case UI_SERVER_LINK_ONLINE:
        return "ONLINE";
    case UI_SERVER_LINK_FAILED:
    default:
        return "OFFLINE";
    }
}

static void ui_refresh_header_wifi(ui_context_t *ui)
{
    mc_wifi_state_t state;

    if (!ui || !ui->header_wifi_icon) return;

    state = wifi_service_poll_state();
    if (state != s_last_wifi_state) {
        if (state == MC_WIFI_CONNECTED) {
            page_chat_clear_runtime_error(ui, "wifi");
        } else if (state == MC_WIFI_FAILED && strcmp(ui->chat_error_source, "wifi") != 0) {
            page_chat_set_runtime_error(ui, "wifi", "disconnected");
        }
        s_last_wifi_state = state;
    }

    ui_set_png_image_src(ui->header_wifi_icon, ui_wifi_icon_src(state), UI_HEADER_WIFI_ICON_ZOOM);
}

static void ui_refresh_header_server(ui_context_t *ui)
{
    if (!ui || !ui->header_server_icon) return;
    ui_set_png_image_src(ui->header_server_icon,
                         ui_server_icon_src(ui->server_link_state),
                         UI_HEADER_SERVER_ICON_ZOOM);
}

static void ui_apply_server_status_now(ui_context_t *ui,
                                       ui_server_status_update_t *update)
{
    if (!ui || !update) return;

    ui->server_link_state = update->state;
    snprintf(ui->server_link_text, sizeof(ui->server_link_text), "%s",
             update->text[0] ? update->text : ui_server_state_text(update->state));
    ui_refresh_header_server(ui);
}

static void ui_apply_server_status_async(void *data)
{
    ui_server_status_update_t *update = (ui_server_status_update_t *)data;

    s_server_probe_busy = false;
    if (update) {
        ui_apply_server_status_now(&s_ui, update);
        free(update);
    }
}

static void ui_server_probe_reset_async(void *data)
{
    (void)data;
    s_server_probe_busy = false;
}

static bool ui_server_link_configured(void)
{
    const char *host = config_get_default("remote", "host", "");
    const char *username = config_get_default("remote", "username", "");
    const char *password = config_get_default("remote", "password", "");

    return host && host[0] && username && username[0] && password && password[0];
}

static void *ui_server_probe_thread(void *arg)
{
    (void)arg;

    remote_status_t status;
    char error[160];
    ui_server_status_update_t *update = calloc(1, sizeof(*update));

    if (!update) {
        lv_async_call(ui_server_probe_reset_async, NULL);
        return NULL;
    }

    if (remote_client_status(&status, error, sizeof(error)) == MC_OK) {
        update->state = UI_SERVER_LINK_ONLINE;
        snprintf(update->text, sizeof(update->text), "%s", "已连接");
    } else {
        update->state = UI_SERVER_LINK_FAILED;
        snprintf(update->text, sizeof(update->text), "%s", "连接失败，请检查配置");
    }

    lv_async_call(ui_apply_server_status_async, update);
    return NULL;
}

static void ui_maybe_probe_server(ui_context_t *ui)
{
    pthread_t worker;
    time_t now;
    ui_server_status_update_t update;

    if (!ui) return;

    if (!ui_server_link_configured()) {
        if (ui->server_link_state != UI_SERVER_LINK_UNCONFIGURED || ui->server_link_text[0] == '\0') {
            memset(&update, 0, sizeof(update));
            update.state = UI_SERVER_LINK_UNCONFIGURED;
            snprintf(update.text, sizeof(update.text), "%s", "填写配置后点 APPLY");
            ui_apply_server_status_now(ui, &update);
        }
        return;
    }

    now = time(NULL);
    if (s_server_probe_busy) {
        return;
    }
    if (!s_server_probe_force && s_last_server_probe_at != 0 &&
        (now - s_last_server_probe_at) < UI_SERVER_PROBE_INTERVAL_S) {
        return;
    }

    s_server_probe_force = false;
    s_server_probe_busy = true;
    s_last_server_probe_at = now;

    if (ui->server_link_state != UI_SERVER_LINK_ONLINE) {
        memset(&update, 0, sizeof(update));
        update.state = UI_SERVER_LINK_CONNECTING;
        snprintf(update.text, sizeof(update.text), "%s", "正在探测服务器");
        ui_apply_server_status_now(ui, &update);
    }

    if (pthread_create(&worker, NULL, ui_server_probe_thread, NULL) != 0) {
        s_server_probe_busy = false;
        memset(&update, 0, sizeof(update));
        update.state = UI_SERVER_LINK_FAILED;
        snprintf(update.text, sizeof(update.text), "%s", "探测启动失败");
        ui_apply_server_status_now(ui, &update);
        return;
    }
    pthread_detach(worker);
}

static void ui_init_ft_font(lv_ft_info_t *info, const char *path, uint16_t weight)
{
    memset(info, 0, sizeof(*info));
    info->name = path;
    info->weight = weight;
    info->style = FT_FONT_STYLE_NORMAL;
    info->mem = NULL;
    info->mem_size = 0;
}

void ui_theme_init(ui_context_t *ui)
{
    ui_style_init(&ui->screen_style);
    lv_style_set_bg_color(&ui->screen_style, lv_color_hex(0x0b1320));
    lv_style_set_border_width(&ui->screen_style, 0);
    lv_style_set_pad_all(&ui->screen_style, 0);
    lv_style_set_radius(&ui->screen_style, 0);

    ui_style_init(&ui->panel_style);
    lv_style_set_bg_color(&ui->panel_style, lv_color_hex(0x142033));
    lv_style_set_border_color(&ui->panel_style, lv_color_hex(0x52d6c5));
    lv_style_set_border_width(&ui->panel_style, 3);
    lv_style_set_radius(&ui->panel_style, 0);
    lv_style_set_pad_all(&ui->panel_style, 10);
    lv_style_set_shadow_width(&ui->panel_style, 0);

    ui_style_init(&ui->panel_alt_style);
    lv_style_set_bg_color(&ui->panel_alt_style, lv_color_hex(0x25193b));
    lv_style_set_border_color(&ui->panel_alt_style, lv_color_hex(0xf4c95d));
    lv_style_set_border_width(&ui->panel_alt_style, 3);
    lv_style_set_radius(&ui->panel_alt_style, 0);
    lv_style_set_pad_all(&ui->panel_alt_style, 10);
    lv_style_set_shadow_width(&ui->panel_alt_style, 0);

    ui_style_init(&ui->card_style);
    lv_style_set_bg_color(&ui->card_style, lv_color_hex(0x17263b));
    lv_style_set_border_color(&ui->card_style, lv_color_hex(0x56d7bf));
    lv_style_set_border_width(&ui->card_style, 2);
    lv_style_set_radius(&ui->card_style, 0);
    lv_style_set_pad_all(&ui->card_style, 10);

    ui_style_init(&ui->card_warn_style);
    lv_style_set_bg_color(&ui->card_warn_style, lv_color_hex(0x32211b));
    lv_style_set_border_color(&ui->card_warn_style, lv_color_hex(0xffb347));
    lv_style_set_border_width(&ui->card_warn_style, 2);
    lv_style_set_radius(&ui->card_warn_style, 0);
    lv_style_set_pad_all(&ui->card_warn_style, 10);

    ui_style_init(&ui->badge_style);
    lv_style_set_bg_color(&ui->badge_style, lv_color_hex(0x0f1f2e));
    lv_style_set_border_color(&ui->badge_style, lv_color_hex(0x63e6be));
    lv_style_set_border_width(&ui->badge_style, 2);
    lv_style_set_radius(&ui->badge_style, 0);
    lv_style_set_pad_all(&ui->badge_style, 6);

    ui_style_init(&ui->badge_alert_style);
    lv_style_set_bg_color(&ui->badge_alert_style, lv_color_hex(0x341f1f));
    lv_style_set_border_color(&ui->badge_alert_style, lv_color_hex(0xff7f50));
    lv_style_set_border_width(&ui->badge_alert_style, 2);
    lv_style_set_radius(&ui->badge_alert_style, 0);
    lv_style_set_pad_all(&ui->badge_alert_style, 6);

    ui_style_init(&ui->text_dim_style);
    lv_style_set_text_color(&ui->text_dim_style, lv_color_hex(0x89a4b8));

    ui_style_init(&ui->text_value_style);
    lv_style_set_text_color(&ui->text_value_style, lv_color_hex(0xf3f7fb));

    ui_style_init(&ui->message_user_style);
    lv_style_set_bg_color(&ui->message_user_style, lv_color_hex(0x16384d));
    lv_style_set_border_color(&ui->message_user_style, lv_color_hex(0x52d6c5));
    lv_style_set_border_width(&ui->message_user_style, 2);
    lv_style_set_radius(&ui->message_user_style, 0);
    lv_style_set_pad_all(&ui->message_user_style, 8);
    lv_style_set_text_color(&ui->message_user_style, lv_color_hex(0xf3f7fb));

    ui_style_init(&ui->message_ai_style);
    lv_style_set_bg_color(&ui->message_ai_style, lv_color_hex(0x2c1f45));
    lv_style_set_border_color(&ui->message_ai_style, lv_color_hex(0xf4c95d));
    lv_style_set_border_width(&ui->message_ai_style, 2);
    lv_style_set_radius(&ui->message_ai_style, 0);
    lv_style_set_pad_all(&ui->message_ai_style, 8);
    lv_style_set_text_color(&ui->message_ai_style, lv_color_hex(0xf8f4ff));
}

void ui_theme_deinit(ui_context_t *ui)
{
    lv_style_reset(&ui->screen_style);
    lv_style_reset(&ui->panel_style);
    lv_style_reset(&ui->panel_alt_style);
    lv_style_reset(&ui->card_style);
    lv_style_reset(&ui->card_warn_style);
    lv_style_reset(&ui->badge_style);
    lv_style_reset(&ui->badge_alert_style);
    lv_style_reset(&ui->text_dim_style);
    lv_style_reset(&ui->text_value_style);
    lv_style_reset(&ui->message_user_style);
    lv_style_reset(&ui->message_ai_style);
}

int ui_font_init(ui_context_t *ui)
{
    const char *font_path = T113CLAW_UI_FONT_REGULAR;

    ui->font_pixel_small = &lv_font_unscii_8;
    ui->font_pixel_large = &lv_font_unscii_16;
    ui->font_cn_small = &lv_font_montserrat_16;
    ui->font_cn_medium = &lv_font_montserrat_20;
    ui->font_cn_large = &lv_font_montserrat_24;
    ui->fonts_ready = false;

    if (access(font_path, R_OK) != 0) {
        LOG_W(TAG, "UI font missing: %s (fallback to built-in fonts)", font_path);
        return MC_OK;
    }

    ui_init_ft_font(&ui->font_small_info, font_path, 18);
    ui_init_ft_font(&ui->font_medium_info, font_path, 24);
    ui_init_ft_font(&ui->font_large_info, font_path, 32);

    if (!lv_ft_font_init(&ui->font_small_info) ||
        !lv_ft_font_init(&ui->font_medium_info) ||
        !lv_ft_font_init(&ui->font_large_info)) {
        LOG_W(TAG, "FreeType font init failed, using built-in fallback");
        if (ui->font_small_info.font) lv_ft_font_destroy(ui->font_small_info.font);
        if (ui->font_medium_info.font) lv_ft_font_destroy(ui->font_medium_info.font);
        if (ui->font_large_info.font) lv_ft_font_destroy(ui->font_large_info.font);
        memset(&ui->font_small_info, 0, sizeof(ui->font_small_info));
        memset(&ui->font_medium_info, 0, sizeof(ui->font_medium_info));
        memset(&ui->font_large_info, 0, sizeof(ui->font_large_info));
        return MC_OK;
    }

    ui->font_cn_small = ui->font_small_info.font;
    ui->font_cn_medium = ui->font_medium_info.font;
    ui->font_cn_large = ui->font_large_info.font;
    ui->fonts_ready = true;
    return MC_OK;
}

void ui_font_deinit(ui_context_t *ui)
{
    if (ui->font_small_info.font) lv_ft_font_destroy(ui->font_small_info.font);
    if (ui->font_medium_info.font) lv_ft_font_destroy(ui->font_medium_info.font);
    if (ui->font_large_info.font) lv_ft_font_destroy(ui->font_large_info.font);
    memset(&ui->font_small_info, 0, sizeof(ui->font_small_info));
    memset(&ui->font_medium_info, 0, sizeof(ui->font_medium_info));
    memset(&ui->font_large_info, 0, sizeof(ui->font_large_info));
    if (ui->fonts_ready) {
        lv_freetype_destroy();
    }
    ui->fonts_ready = false;
}

void ui_set_header_clock(ui_context_t *ui, const char *text)
{
    if (!ui->header_clock) return;
    lv_label_set_text(ui->header_clock, text ? text : "--:--:--");
}

void ui_page_show(ui_context_t *ui, ui_page_t page)
{
    if (page >= UI_PAGE_COUNT) return;

    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        if (!ui->pages[i]) continue;

        if (i == (int)page) {
            lv_obj_clear_flag(ui->pages[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui->pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    ui->current_page = page;
    if (ui->header_title) {
        lv_label_set_text(ui->header_title, page_titles[page]);
    }

    page_settings_refresh(ui);
}

static void ui_header_settings_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_CLICKED) {
        return;
    }

    if (s_ui.current_page != UI_PAGE_SETTINGS) {
        LOG_I(TAG, "UI page -> %s", page_titles[UI_PAGE_SETTINGS]);
        ui_page_show(&s_ui, UI_PAGE_SETTINGS);
    }
    lv_event_stop_bubbling(event);
}

static void ui_header_gesture_event_cb(lv_event_t *event)
{
    lv_indev_t *indev;
    lv_dir_t dir;

    if (lv_event_get_code(event) != LV_EVENT_GESTURE) {
        return;
    }

    if (s_ui.current_page != UI_PAGE_CHAT) {
        return;
    }

    indev = lv_event_get_indev(event);
    if (!indev) {
        return;
    }

    dir = lv_indev_get_gesture_dir(indev);
    if (dir != LV_DIR_BOTTOM) {
        return;
    }

    LOG_I(TAG, "UI page -> %s (header swipe down)", page_titles[UI_PAGE_SETTINGS]);
    ui_page_show(&s_ui, UI_PAGE_SETTINGS);
    lv_event_stop_bubbling(event);
}

static void ui_build_shell(ui_context_t *ui)
{
    const lv_coord_t header_h = 30;
    const lv_coord_t content_y = 32;
    const lv_coord_t content_h = 248;

    lv_obj_t *screen = lv_scr_act();
    lv_obj_remove_style_all(screen);
    lv_obj_add_style(screen, &ui->screen_style, 0);

    ui->root = lv_obj_create(screen);
    lv_obj_set_size(ui->root, LV_PCT(100), LV_PCT(100));
    lv_obj_add_style(ui->root, &ui->screen_style, 0);
    lv_obj_clear_flag(ui->root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(ui->root);
    lv_obj_set_size(header, LV_PCT(100), header_h);
    lv_obj_set_align(header, LV_ALIGN_TOP_MID);
    lv_obj_add_style(header, &ui->panel_style, 0);
    lv_obj_set_style_pad_left(header, 10, 0);
    lv_obj_set_style_pad_right(header, 10, 0);
    lv_obj_set_style_pad_top(header, 2, 0);
    lv_obj_set_style_pad_bottom(header, 2, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(header, ui_header_gesture_event_cb, LV_EVENT_GESTURE, NULL);

    ui->header_title = lv_label_create(header);
    lv_obj_set_style_text_font(ui->header_title, ui->font_pixel_large, 0);
    lv_obj_set_style_text_color(ui->header_title, lv_color_hex(0xf4c95d), 0);
    lv_label_set_text(ui->header_title, page_titles[UI_PAGE_CHAT]);
    lv_obj_align(ui->header_title, LV_ALIGN_LEFT_MID, 8, 0);

    ui->header_clock = lv_label_create(header);
    lv_obj_set_style_text_font(ui->header_clock, ui->font_pixel_large, 0);
    lv_obj_set_style_text_color(ui->header_clock, lv_color_hex(0x63e6be), 0);
    lv_label_set_text(ui->header_clock, "00:00:00");
    lv_obj_align(ui->header_clock, LV_ALIGN_RIGHT_MID, -8, 0);

    lv_obj_t *header_settings_wrap = lv_obj_create(header);
    lv_obj_remove_style_all(header_settings_wrap);
    lv_obj_set_size(header_settings_wrap, UI_HEADER_SETTINGS_WRAP_W, UI_HEADER_SETTINGS_WRAP_H);
    lv_obj_clear_flag(header_settings_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(header_settings_wrap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(header_settings_wrap, ui_header_settings_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(header_settings_wrap, ui_header_settings_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *header_server_wrap = lv_obj_create(header);
    lv_obj_remove_style_all(header_server_wrap);
    lv_obj_set_size(header_server_wrap, UI_HEADER_SERVER_WRAP_W, UI_HEADER_SERVER_WRAP_H);
    lv_obj_clear_flag(header_server_wrap, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header_wifi_wrap = lv_obj_create(header);
    lv_obj_remove_style_all(header_wifi_wrap);
    lv_obj_set_size(header_wifi_wrap, UI_HEADER_WIFI_WRAP_W, UI_HEADER_WIFI_WRAP_H);
    lv_obj_clear_flag(header_wifi_wrap, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header_settings_icon = ui_create_png_image(header_settings_wrap, UI_FS_SRC(T113CLAW_UI_ICON_SETTINGS), UI_HEADER_SETTINGS_ICON_ZOOM);
    lv_obj_center(header_settings_icon);

    ui->header_server_icon = ui_create_png_image(header_server_wrap,
                                                 ui_server_icon_src(ui->server_link_state),
                                                 UI_HEADER_SERVER_ICON_ZOOM);
    lv_obj_center(ui->header_server_icon);

    ui->header_wifi_icon = ui_create_png_image(header_wifi_wrap, ui_wifi_icon_src(wifi_service_get_state()), UI_HEADER_WIFI_ICON_ZOOM);
    lv_obj_center(ui->header_wifi_icon);
    lv_obj_align_to(header_wifi_wrap, ui->header_clock, LV_ALIGN_OUT_LEFT_MID, -6, 0);
    lv_obj_align_to(header_server_wrap, header_wifi_wrap, LV_ALIGN_OUT_LEFT_MID, -6, 0);
    lv_obj_align_to(header_settings_wrap, header_server_wrap, LV_ALIGN_OUT_LEFT_MID, -6, 0);

    lv_obj_t *content = lv_obj_create(ui->root);
    lv_obj_set_pos(content, 0, content_y);
    lv_obj_set_size(content, LV_PCT(100), content_h);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 4, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        ui->pages[i] = lv_obj_create(content);
        lv_obj_set_size(ui->pages[i], LV_PCT(100), LV_PCT(100));
        lv_obj_set_align(ui->pages[i], LV_ALIGN_TOP_MID);
        lv_obj_set_style_bg_opa(ui->pages[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ui->pages[i], 0, 0);
        lv_obj_set_style_pad_all(ui->pages[i], 0, 0);
        lv_obj_clear_flag(ui->pages[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    page_chat_build(ui, ui->pages[UI_PAGE_CHAT]);
    page_settings_build(ui, ui->pages[UI_PAGE_SETTINGS]);
    ui_page_show(ui, UI_PAGE_CHAT);
}

static void ui_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    char time_buf[32];
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm_now);
    ui_set_header_clock(&s_ui, time_buf);

    ui_refresh_header_wifi(&s_ui);
    ui_maybe_probe_server(&s_ui);
    page_settings_refresh(&s_ui);
}

static void ui_async_apply(void *data)
{
    ui_async_event_t *event = (ui_async_event_t *)data;
    if (!event) return;

    switch (event->type) {
    case UI_EVT_USER_MESSAGE:
        page_chat_append(&s_ui, "user", event->meta, event->text);
        break;
    case UI_EVT_ASSISTANT_MESSAGE:
        page_chat_append(&s_ui, "assistant", event->meta, event->text);
        break;
    case UI_EVT_VOICE_STATE:
        page_chat_set_voice_state(&s_ui, event->text);
        break;
    case UI_EVT_AGENT_STATE:
        page_chat_set_agent_state(&s_ui, event->text);
        break;
    case UI_EVT_RUNTIME_ERROR:
        page_chat_set_runtime_error(&s_ui, event->meta, event->text);
        break;
    }

    page_settings_refresh(&s_ui);
    free(event);
}

static void ui_dispatch_async(ui_async_event_type_t type, const char *meta, const char *text)
{
    if (!s_initialized) return;

    ui_async_event_t *event = calloc(1, sizeof(*event));
    if (!event) return;

    event->type = type;
    if (meta) snprintf(event->meta, sizeof(event->meta), "%s", meta);
    if (text) snprintf(event->text, sizeof(event->text), "%s", text);
    lv_async_call(ui_async_apply, event);
}

int ui_manager_init(void)
{
    memset(&s_ui, 0, sizeof(s_ui));
    snprintf(s_ui.current_channel, sizeof(s_ui.current_channel), "%s", "cli");
    snprintf(s_ui.voice_state, sizeof(s_ui.voice_state), "%s", "待命");
    snprintf(s_ui.agent_state, sizeof(s_ui.agent_state), "%s", "READY");
    s_ui.server_link_state = UI_SERVER_LINK_UNCONFIGURED;
    snprintf(s_ui.server_link_text, sizeof(s_ui.server_link_text), "%s",
             "填写配置后点 APPLY");
    s_ui.chat_follow_tail = true;
    s_last_wifi_state = wifi_service_get_state();
    s_server_probe_busy = false;
    s_server_probe_force = true;
    s_last_server_probe_at = 0;

    lv_init();
    lv_port_disp_init(false);
    lv_port_indev_init();

    ui_theme_init(&s_ui);
    ui_font_init(&s_ui);
    ui_build_shell(&s_ui);

    page_chat_append(&s_ui, "system", "ui", "LVGL UI online. Ready for T113Claw messages.");

    s_refresh_timer = lv_timer_create(ui_refresh_timer_cb, 1000, NULL);
    ui_refresh_timer_cb(NULL);
    s_initialized = 1;

    LOG_I(TAG, "UI manager initialized (LVGL active)");
    return MC_OK;
}

void ui_manager_update(void)
{
    if (!s_initialized) return;
    lv_timer_handler();
}

void ui_manager_shutdown(void)
{
    if (!s_initialized) return;

    if (s_refresh_timer) {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = NULL;
    }

    ui_font_deinit(&s_ui);
    ui_theme_deinit(&s_ui);
    lv_port_indev_deinit();
    lv_port_disp_deinit();
    s_initialized = 0;
}

void ui_manager_notify_user_message(const char *channel, const char *text)
{
    ui_dispatch_async(UI_EVT_USER_MESSAGE, channel, text);
}

void ui_manager_notify_assistant_message(const char *channel, const char *text)
{
    ui_dispatch_async(UI_EVT_ASSISTANT_MESSAGE, channel, text);
}

void ui_manager_notify_voice_state(const char *state)
{
    ui_dispatch_async(UI_EVT_VOICE_STATE, NULL, state);
}

void ui_manager_notify_agent_state(const char *state)
{
    ui_dispatch_async(UI_EVT_AGENT_STATE, NULL, state);
}

void ui_manager_request_server_probe(void)
{
    s_server_probe_force = true;
    if (s_initialized) {
        ui_maybe_probe_server(&s_ui);
    }
}

void mc_log_error_sink(const char *tag, const char *message)
{
    ui_dispatch_async(UI_EVT_RUNTIME_ERROR, tag, message);
}

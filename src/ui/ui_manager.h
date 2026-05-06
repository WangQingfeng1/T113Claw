#pragma once
/*
 * T113Claw UI Manager
 *
 * Owns the LVGL shell, page switching and runtime UI notifications for
 * chat and settings pages on both T113 and the x86 SDL simulator.
 */

int ui_manager_init(void);
void ui_manager_update(void);
void ui_manager_shutdown(void);

void ui_manager_notify_user_message(const char *channel, const char *text);
void ui_manager_notify_assistant_message(const char *channel, const char *text);
void ui_manager_notify_voice_state(const char *state);
void ui_manager_notify_agent_state(const char *state);
void ui_manager_request_server_probe(void);

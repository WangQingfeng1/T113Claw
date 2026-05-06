#pragma once
/*
 * T113Claw WiFi Service
 *
 * Automatic WiFi connection for T113-S3 board.
 * Reads SSID/password from config (build-time secrets or config.ini),
 * manages wpa_supplicant, DHCP, DNS, and NTP time sync.
 *
 * On x86 (SIMULATOR_LINUX) all operations are no-ops.
 */

/* WiFi connection states */
typedef enum {
    MC_WIFI_OFF = 0,
    MC_WIFI_CONNECTING,
    MC_WIFI_CONNECTED,
    MC_WIFI_FAILED,
} mc_wifi_state_t;

/* Initialize WiFi service (reads config, no network action yet) */
int wifi_service_init(void);

/* Start WiFi: bring up interface, connect, DHCP, NTP.
 * Blocks until connected or timeout (30s). Returns 0 on success. */
int wifi_service_start(void);

/* Stop WiFi service (release resources) */
void wifi_service_stop(void);

/* Query current WiFi state */
mc_wifi_state_t wifi_service_get_state(void);

/* Refresh current WiFi link state without reconnecting */
mc_wifi_state_t wifi_service_poll_state(void);

/* Check if network is reachable (quick ping test) */
int wifi_service_check_network(void);

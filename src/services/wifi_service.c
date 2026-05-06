/*
 * T113Claw WiFi Service — T113-S3 Linux WiFi Auto-Connection
 *
 * Uses wpa_cli (command-line) to manage wpa_supplicant on the board.
 * No libwpa_client dependency — works with any TinaLinux firmware.
 *
 * Reference: reference_linux/app_sdk/component/wifi/wpa_manager.c
 *
 * Boot sequence:
 *   1. ifconfig wlan0 up
 *   2. Start wpa_supplicant (if not running)
 *   3. Configure network via wpa_cli
 *   4. Wait for COMPLETED state
 *   5. udhcpc for DHCP
 *   6. Set DNS resolvers
 *   7. NTP time sync (critical for TLS)
 */

#include "wifi_service.h"
#include "config/config.h"
#include "t113claw_config.h"
#include "utils/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define TAG "wifi"

/* ── Configuration ────────────────────────────────────────── */
#define WIFI_IFACE          "wlan0"
#define WPA_CONF_PATH       "/etc/wifi/wpa_supplicant/wpa_supplicant.conf"
#define WPA_SOCK_DIR        "/etc/wifi/wpa_supplicant/sockets"
#define WPA_CLI_CMD         "wpa_cli -p " WPA_SOCK_DIR " -i " WIFI_IFACE
#define WIFI_CONNECT_TIMEOUT_S  30
#define NTP_SERVER          "ntp.aliyun.com"

static mc_wifi_state_t s_state = MC_WIFI_OFF;

#ifndef SIMULATOR_LINUX
/* ── Helper: run a command and capture stdout ─────────────── */
static int run_cmd(const char *cmd, char *out, size_t out_size)
{
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    if (out && out_size > 0) {
        out[0] = '\0';
        size_t total = 0;
        while (total < out_size - 1) {
            size_t n = fread(out + total, 1, out_size - 1 - total, fp);
            if (n == 0) break;
            total += n;
        }
        out[total] = '\0';
    }

    return pclose(fp);
}

/* ── Helper: run a command silently ───────────────────────── */
static int run_silent(const char *cmd)
{
    char buf[256];
    return run_cmd(cmd, buf, sizeof(buf));
}

/* ── Helper: check if a process is running ────────────────── */
static int is_process_running(const char *name)
{
    char cmd[128];
    char buf[64];
    snprintf(cmd, sizeof(cmd), "ps | grep %s | grep -v grep", name);
    run_cmd(cmd, buf, sizeof(buf));
    return buf[0] != '\0';
}

/* ── WiFi: bring up wpa_supplicant ────────────────────────── */
static int wifi_start_supplicant(void)
{
    if (is_process_running("wpa_supplicant")) {
        LOG_D(TAG, "wpa_supplicant already running");
        return 0;
    }

    LOG_I(TAG, "Starting wpa_supplicant...");

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ifconfig %s up 2>&1", WIFI_IFACE);
    if (run_silent(cmd) != 0) {
        LOG_E(TAG, "Failed to bring up %s", WIFI_IFACE);
        return -1;
    }

    snprintf(cmd, sizeof(cmd),
             "wpa_supplicant -i %s -c %s -B 2>&1",
             WIFI_IFACE, WPA_CONF_PATH);
    if (run_silent(cmd) != 0) {
        LOG_E(TAG, "Failed to start wpa_supplicant");
        return -1;
    }

    /* Wait for control socket to appear */
    for (int i = 0; i < 10; i++) {
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", WPA_SOCK_DIR, WIFI_IFACE);
        if (access(path, F_OK) == 0) return 0;
        usleep(500000); /* 500ms */
    }

    LOG_W(TAG, "wpa_supplicant control socket slow to appear");
    return 0;
}

/* ── WiFi: check if already connected ─────────────────────── */
static int wifi_is_connected(void)
{
    char buf[512];
    run_cmd(WPA_CLI_CMD " status 2>/dev/null", buf, sizeof(buf));
    return strstr(buf, "wpa_state=COMPLETED") != NULL;
}

static mc_wifi_state_t wifi_state_from_status(const char *status)
{
    if (!status || status[0] == '\0') {
        return MC_WIFI_FAILED;
    }

    if (strstr(status, "wpa_state=COMPLETED")) {
        return MC_WIFI_CONNECTED;
    }

    if (strstr(status, "wpa_state=SCANNING") ||
        strstr(status, "wpa_state=ASSOCIATING") ||
        strstr(status, "wpa_state=ASSOCIATED") ||
        strstr(status, "wpa_state=4WAY_HANDSHAKE") ||
        strstr(status, "wpa_state=GROUP_HANDSHAKE")) {
        return MC_WIFI_CONNECTING;
    }

    return MC_WIFI_FAILED;
}

static int wifi_extract_status_value(const char *status,
                                     const char *key,
                                     char *out,
                                     size_t out_size)
{
    const char *pos;
    const char *end;
    size_t key_len;
    size_t len;

    if (!status || !key || !out || out_size == 0) {
        return -1;
    }

    key_len = strlen(key);
    pos = status;
    while ((pos = strstr(pos, key)) != NULL) {
        if (pos == status || pos[-1] == '\n') {
            pos += key_len;
            end = strpbrk(pos, "\r\n");
            len = end ? (size_t)(end - pos) : strlen(pos);
            if (len >= out_size) {
                len = out_size - 1;
            }
            memcpy(out, pos, len);
            out[len] = '\0';
            return 0;
        }
        pos += key_len;
    }

    out[0] = '\0';
    return -1;
}

static int wifi_status_matches_ssid(const char *status, const char *ssid)
{
    char current_ssid[128];

    if (!ssid || ssid[0] == '\0') {
        return 0;
    }

    if (wifi_extract_status_value(status, "ssid=", current_ssid, sizeof(current_ssid)) != 0) {
        return 0;
    }

    return strcmp(current_ssid, ssid) == 0;
}

/* ── WiFi: configure and connect ──────────────────────────── */
static int wifi_configure(const char *ssid, const char *pass)
{
    char cmd[256];
    char buf[512];

    /* Check if already connected to the right SSID */
    if (wifi_is_connected()) {
        run_cmd(WPA_CLI_CMD " status 2>/dev/null", buf, sizeof(buf));
        if (wifi_status_matches_ssid(buf, ssid)) {
            LOG_I(TAG, "Already connected to %s", ssid);
            return 0;
        }
    }

    LOG_I(TAG, "Configuring WiFi: %s", ssid);

    /* Remove existing networks */
    run_silent(WPA_CLI_CMD " remove_network all 2>/dev/null");

    /* Add new network */
    run_cmd(WPA_CLI_CMD " add_network 2>/dev/null", buf, sizeof(buf));

    /* Set SSID and PSK */
    snprintf(cmd, sizeof(cmd),
             WPA_CLI_CMD " set_network 0 ssid '\"%s\"' 2>/dev/null", ssid);
    run_silent(cmd);

    snprintf(cmd, sizeof(cmd),
             WPA_CLI_CMD " set_network 0 psk '\"%s\"' 2>/dev/null", pass);
    run_silent(cmd);

    /* Enable and select */
    run_silent(WPA_CLI_CMD " enable_network 0 2>/dev/null");
    run_silent(WPA_CLI_CMD " select_network 0 2>/dev/null");

    /* Wait for connection */
    LOG_I(TAG, "Waiting for WiFi connection (timeout %ds)...", WIFI_CONNECT_TIMEOUT_S);
    for (int i = 0; i < WIFI_CONNECT_TIMEOUT_S; i++) {
        if (wifi_is_connected()) {
            LOG_I(TAG, "WiFi connected to %s", ssid);

            /* Save config so it auto-connects next time */
            run_silent(WPA_CLI_CMD " save_config 2>/dev/null");
            return 0;
        }
        sleep(1);
    }

    LOG_E(TAG, "WiFi connection timeout (%ds)", WIFI_CONNECT_TIMEOUT_S);
    return -1;
}

/* ── DHCP ─────────────────────────────────────────────────── */
static int wifi_dhcp(void)
{
    char cmd[128];
    char buf[256];

    /* Check if we already have an IP */
    snprintf(cmd, sizeof(cmd), "ip addr show %s 2>/dev/null", WIFI_IFACE);
    run_cmd(cmd, buf, sizeof(buf));
    if (strstr(buf, "inet ") && !strstr(buf, "inet 0.")) {
        LOG_D(TAG, "IP already assigned");
        return 0;
    }

    LOG_I(TAG, "Requesting DHCP lease...");
    snprintf(cmd, sizeof(cmd),
             "udhcpc -i %s -t 5 -T 2 -A 5 -q 2>&1", WIFI_IFACE);
    int rc = run_silent(cmd);
    if (rc != 0) {
        LOG_E(TAG, "DHCP failed");
    }
    return rc;
}

/* ── DNS ──────────────────────────────────────────────────── */
static void wifi_setup_dns(void)
{
    /* Only configure if resolv.conf is empty or missing */
    char buf[128] = {0};
    run_cmd("cat /etc/resolv.conf 2>/dev/null", buf, sizeof(buf));
    if (strstr(buf, "nameserver") && !strstr(buf, "nameserver 127.")) {
        return; /* DNS already configured */
    }

    LOG_I(TAG, "Setting up DNS resolvers");
    run_silent("echo 'nameserver 114.114.114.114' > /etc/resolv.conf");
    run_silent("echo 'nameserver 8.8.8.8' >> /etc/resolv.conf");
}

/* ── NTP time sync ────────────────────────────────────────── */
static void wifi_sync_time(void)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    /* If year < 2024, clock is clearly wrong (no RTC battery) */
    if (t->tm_year + 1900 < 2024) {
        LOG_W(TAG, "System clock is wrong (%d), attempting NTP sync...",
              t->tm_year + 1900);

        /* BusyBox ntpd can't handle huge offsets from 1970.
         * Set a rough date first so TLS works, then fine-tune. */
        run_silent("date -s '2026-01-01 00:00:00' 2>/dev/null");
    }

    /* Try NTP sync (non-blocking, best effort) */
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "ntpd -q -p %s 2>/dev/null &", NTP_SERVER);
    run_silent(cmd);
    LOG_I(TAG, "NTP time sync initiated (%s)", NTP_SERVER);
}
#endif /* !SIMULATOR_LINUX */

/* ── Public API ───────────────────────────────────────────── */

int wifi_service_init(void)
{
#ifdef SIMULATOR_LINUX
    LOG_I(TAG, "WiFi service initialized (simulator — no-op)");
    s_state = MC_WIFI_CONNECTED; /* Assume host has network */
    return 0;
#else
    s_state = MC_WIFI_OFF;
    LOG_I(TAG, "WiFi service initialized");
    return 0;
#endif
}

int wifi_service_start(void)
{
#ifdef SIMULATOR_LINUX
    LOG_I(TAG, "WiFi service started (simulator — skipped)");
    return 0;
#else
    const char *ssid = config_get("wifi", "ssid");
    const char *pass = config_get("wifi", "pass");

    /* If no WiFi credentials configured, skip entirely */
    if (!ssid || ssid[0] == '\0') {
        LOG_W(TAG, "No WiFi SSID configured, skipping auto-connect");
        LOG_W(TAG, "Set wifi ssid/pass in t113claw_secrets.h or config.ini");

        /* Still try to check if network is already available */
        if (wifi_service_check_network() == 0) {
            LOG_I(TAG, "Network already available (WiFi configured externally)");
            s_state = MC_WIFI_CONNECTED;
            wifi_sync_time();
            return 0;
        }
        s_state = MC_WIFI_OFF;
        return -1;
    }

    s_state = MC_WIFI_CONNECTING;

    /* Step 1: Start wpa_supplicant */
    if (wifi_start_supplicant() != 0) {
        s_state = MC_WIFI_FAILED;
        return -1;
    }

    /* Step 2: Configure and connect */
    if (wifi_configure(ssid, pass ? pass : "") != 0) {
        s_state = MC_WIFI_FAILED;
        return -1;
    }

    /* Step 3: Get IP via DHCP */
    if (wifi_dhcp() != 0) {
        s_state = MC_WIFI_FAILED;
        return -1;
    }

    /* Step 4: Setup DNS */
    wifi_setup_dns();

    /* Step 5: Sync time (important for TLS) */
    wifi_sync_time();

    s_state = MC_WIFI_CONNECTED;
    LOG_I(TAG, "WiFi service started — network ready");
    return 0;
#endif
}

void wifi_service_stop(void)
{
#ifndef SIMULATOR_LINUX
    run_silent(WPA_CLI_CMD " disconnect 2>/dev/null");
#endif
    LOG_I(TAG, "WiFi service stopped");
    s_state = MC_WIFI_OFF;
}

mc_wifi_state_t wifi_service_get_state(void)
{
    return s_state;
}

mc_wifi_state_t wifi_service_poll_state(void)
{
#ifdef SIMULATOR_LINUX
    return s_state;
#else
    char buf[512] = {0};
    mc_wifi_state_t prev = s_state;

    if (s_state == MC_WIFI_OFF) {
        return s_state;
    }

    if (run_cmd(WPA_CLI_CMD " status 2>/dev/null", buf, sizeof(buf)) != 0) {
        s_state = MC_WIFI_FAILED;
    } else {
        s_state = wifi_state_from_status(buf);
    }

    if (s_state != prev) {
        switch (s_state) {
        case MC_WIFI_CONNECTED:
            LOG_I(TAG, "WiFi link restored");
            break;
        case MC_WIFI_CONNECTING:
            LOG_W(TAG, "WiFi reconnecting");
            break;
        case MC_WIFI_FAILED:
            LOG_W(TAG, "WiFi link lost");
            break;
        case MC_WIFI_OFF:
        default:
            break;
        }
    }

    return s_state;
#endif
}

int wifi_service_check_network(void)
{
#ifdef SIMULATOR_LINUX
    return 0; /* Assume host has network */
#else
    char buf[128];
    int rc = run_cmd("ping -c 1 -W 3 114.114.114.114 2>/dev/null", buf, sizeof(buf));
    return (rc == 0) ? 0 : -1;
#endif
}

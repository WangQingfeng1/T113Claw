/*
 * T113Claw Audio — Codec Initialization
 *
 * Configures the T113-S3 built-in audio codec via amixer.
 * Hardware: MIC3 (differential) → ADC3, DAC → HPOUT → LM4871 amp
 * Amplifier control: GPIO34 (PB2) — LOW=on, HIGH=off
 */

#include "audio_codec.h"
#include "audio_ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define TAG "codec"
#define LOG_I(tag, fmt, ...) fprintf(stderr, "[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_E(tag, fmt, ...) fprintf(stderr, "[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...) fprintf(stderr, "[W][%s] " fmt "\n", tag, ##__VA_ARGS__)

/* Current volume (0-100) */
static int s_volume = 80;

/* Run an amixer command, return 0 on success */
static int amixer_set(const char *cmd)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "amixer -c 0 cset %s 2>/dev/null", cmd);
    int rc = system(buf);
    if (rc != 0) {
        LOG_W(TAG, "amixer command failed: %s", cmd);
    }
    return rc == 0 ? 0 : -1;
}

/*
 * Initialize the T113-S3 audio codec.
 *
 * Key mixer controls (from amixer -c 0 contents):
 *   numid=29  ADC3 Input MIC3 Boost Switch  → on (enable MIC3 capture)
 *   numid=22  MIC3 Input Select             → MIC_DIFFER (differential)
 *   numid=11  MIC3 gain volume              → 31 (max gain)
 *   numid=8   ADC3 volume                   → 160
 *   numid=30  Headphone Switch              → on
 *   numid=17  Headphone volume              → 6 (near max)
 *   numid=5   DAC volume                    → 160 160
 *   numid=4   digital volume                → 63 (max)
 */
int audio_codec_init(void)
{
    LOG_I(TAG, "Initializing T113-S3 audio codec...");

    /* Enable MIC3 input path */
    amixer_set("numid=29 1");       /* ADC3 Input MIC3 Boost Switch = on */
    amixer_set("numid=22 0");       /* MIC3 Input Select = MIC_DIFFER */
    amixer_set("numid=11 31");      /* MIC3 gain volume = 31 (max) */
    amixer_set("numid=8 160");      /* ADC3 volume = 160 */

    /* Enable headphone output path */
    amixer_set("numid=30 1");       /* Headphone Switch = on */
    amixer_set("numid=17 6");       /* Headphone volume = 6 */

    /* DAC settings */
    amixer_set("numid=5 160,160");  /* DAC volume = 160 160 (L/R) */
    amixer_set("numid=4 63");       /* digital volume = 63 (max) */

    /* Disable unused inputs to reduce noise */
    amixer_set("numid=24 0");       /* ADC1 Input FMINL Switch = off */
    amixer_set("numid=25 0");       /* ADC1 Input LINEINL Switch = off */
    amixer_set("numid=23 0");       /* ADC1 Input MIC1 Boost Switch = off */
    amixer_set("numid=26 0");       /* ADC2 Input MIC2 Boost Switch = off */

    LOG_I(TAG, "Audio codec initialized (MIC3 input, HP output)");
    return 0;
}

/*
 * Set playback volume.
 * volume_pct: 0-100 → mapped to DAC volume 0-255 and HP volume 0-7.
 * Keep a small non-zero floor so low-volume adjustments don't collapse
 * immediately into silence on the headphone step map.
 */
int audio_codec_set_volume(int volume_pct)
{
    if (volume_pct < 0) volume_pct = 0;
    if (volume_pct > 100) volume_pct = 100;
    s_volume = volume_pct;

    int dac_vol = 0;
    int hp_vol = 0;

    if (volume_pct > 0) {
        dac_vol = 36 + (volume_pct * (255 - 36) + 99) / 100;
        if (dac_vol > 255) dac_vol = 255;

        hp_vol = 1 + ((volume_pct - 1) * 6 + 98) / 99;
        if (hp_vol > 7) hp_vol = 7;
    }

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "numid=5 %d,%d", dac_vol, dac_vol);
    amixer_set(cmd);

    snprintf(cmd, sizeof(cmd), "numid=17 %d", hp_vol);
    amixer_set(cmd);

    LOG_I(TAG, "Volume set to %d%% (DAC=%d, HP=%d)", volume_pct, dac_vol, hp_vol);
    return 0;
}

int audio_codec_get_volume(void)
{
    return s_volume;
}

/*
 * Enable/disable the LM4871 amplifier via GPIO34 (PB2).
 * Hardware: PA_SHDN pin — LOW = amplifier ON, HIGH = amplifier OFF
 */
int audio_amp_enable(int enable)
{
    int fd = open(AUDIO_AMP_GPIO_PATH, O_WRONLY);
    if (fd < 0) {
        /* Try to export the GPIO first */
        int efd = open("/sys/class/gpio/export", O_WRONLY);
        if (efd >= 0) {
            char gpio_str[8];
            int len = snprintf(gpio_str, sizeof(gpio_str), "%d", AUDIO_AMP_GPIO);
            write(efd, gpio_str, len);
            close(efd);
            usleep(100000); /* wait for sysfs */
        }

        /* Set direction to output */
        char dir_path[64];
        snprintf(dir_path, sizeof(dir_path), "/sys/class/gpio/gpio%d/direction", AUDIO_AMP_GPIO);
        int dfd = open(dir_path, O_WRONLY);
        if (dfd >= 0) {
            write(dfd, "out", 3);
            close(dfd);
        }

        fd = open(AUDIO_AMP_GPIO_PATH, O_WRONLY);
        if (fd < 0) {
            LOG_E(TAG, "Cannot open GPIO%d for amplifier control", AUDIO_AMP_GPIO);
            return -1;
        }
    }

    /* LOW = amp ON, HIGH = amp OFF (inverted logic) */
    const char *val = enable ? "0" : "1";
    write(fd, val, 1);
    close(fd);

    LOG_I(TAG, "Amplifier %s", enable ? "enabled" : "disabled");
    return 0;
}

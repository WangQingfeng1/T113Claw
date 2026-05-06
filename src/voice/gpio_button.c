/*
 * GPIO Button Detection via Linux sysfs
 *
 * Exports a GPIO pin, sets direction=in, edge=both,
 * then polls for events in a background thread.
 */

#include "gpio_button.h"
#include "utils/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>

#define TAG "button"
#define DEBOUNCE_MS 50  /* Ignore events within 50ms of last change */

static int            s_gpio_num = -1;
static int            s_value_fd = -1;
static pthread_t      s_thread;
static volatile int   s_running;
static gpio_button_cb_t s_cb;
static void          *s_user;

/* ── sysfs helpers ────────────────────────────────────────── */

static int write_file(const char *path, const char *val)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    int rc = write(fd, val, strlen(val));
    close(fd);
    return rc > 0 ? 0 : -1;
}

static int gpio_export(int num)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", num);
    if (access(path, F_OK) == 0)
        return 0; /* already exported */

    char buf[16];
    snprintf(buf, sizeof(buf), "%d", num);
    return write_file("/sys/class/gpio/export", buf);
}

static int gpio_unexport(int num)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", num);
    return write_file("/sys/class/gpio/unexport", buf);
}

static int gpio_setup(int num)
{
    char path[128];

    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", num);
    if (write_file(path, "in") < 0) return -1;

    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/edge", num);
    if (write_file(path, "both") < 0) return -1;

    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", num);
    int fd = open(path, O_RDONLY);
    return fd;
}

static int gpio_read_value(int fd)
{
    char buf[4];
    lseek(fd, 0, SEEK_SET);
    int n = read(fd, buf, sizeof(buf));
    if (n <= 0) return -1;
    return buf[0] == '1' ? 1 : 0;
}

/* ── Polling thread ───────────────────────────────────────── */

static void *poll_thread(void *arg)
{
    (void)arg;
    struct pollfd pfd;
    pfd.fd = s_value_fd;
    pfd.events = POLLPRI | POLLERR;

    /* Initial read to get baseline value and clear pending interrupt */
    int last_val = gpio_read_value(s_value_fd);
    struct timeval last_change = {0, 0};

    LOG_I(TAG, "Poll thread started, fd=%d, initial val=%d", s_value_fd, last_val);

    while (s_running) {
        int ret = poll(&pfd, 1, 200); /* 200ms timeout */
        if (ret < 0) {
            if (s_running) LOG_E(TAG, "poll error: %s", strerror(errno));
            break;
        }
        /* Read value on both edge events AND timeout.
         * Fallback polling handles platforms where GPIO
         * interrupts are not supported (e.g. T113 port D). */
        int val = gpio_read_value(s_value_fd);

        if (val >= 0 && val != last_val) {
            /* Debounce: ignore changes within DEBOUNCE_MS of last change.
             * Use long long to avoid 32-bit overflow on ARM.
             * Skip debounce for the very first change (last_change == 0). */
            struct timeval now;
            gettimeofday(&now, NULL);
            long long elapsed_ms = (long long)(now.tv_sec - last_change.tv_sec) * 1000 +
                             (now.tv_usec - last_change.tv_usec) / 1000;
            if (last_change.tv_sec != 0 && elapsed_ms < DEBOUNCE_MS) continue;

            last_change = now;
            last_val = val;
            /* Button typically active-low: pressed=0, released=1 */
            int pressed = (val == 0) ? 1 : 0;
            LOG_I(TAG, "Button %s (gpio%d val=%d)",
                  pressed ? "PRESS" : "RELEASE", s_gpio_num, val);
            if (s_cb)
                s_cb(pressed, s_user);
        }
    }

    return NULL;
}

/* ── Public API ───────────────────────────────────────────── */

int gpio_button_start(int gpio_num, gpio_button_cb_t cb, void *user)
{
    if (gpio_num < 0 || !cb) return -1;

    s_gpio_num = gpio_num;
    s_cb = cb;
    s_user = user;

    if (gpio_export(gpio_num) < 0) {
        LOG_E(TAG, "GPIO %d export failed", gpio_num);
        return -1;
    }

    /* Small delay for sysfs to settle */
    usleep(100000);

    s_value_fd = gpio_setup(gpio_num);
    if (s_value_fd < 0) {
        LOG_E(TAG, "GPIO %d setup failed", gpio_num);
        gpio_unexport(gpio_num);
        return -1;
    }

    s_running = 1;
    if (pthread_create(&s_thread, NULL, poll_thread, NULL) != 0) {
        LOG_E(TAG, "Failed to start button thread");
        close(s_value_fd);
        gpio_unexport(gpio_num);
        return -1;
    }

    LOG_I(TAG, "Button started on GPIO %d", gpio_num);
    return 0;
}

void gpio_button_stop(void)
{
    if (!s_running) return;

    s_running = 0;
    pthread_join(s_thread, NULL);

    if (s_value_fd >= 0) {
        close(s_value_fd);
        s_value_fd = -1;
    }

    if (s_gpio_num >= 0) {
        gpio_unexport(s_gpio_num);
        s_gpio_num = -1;
    }

    LOG_I(TAG, "Button stopped");
}

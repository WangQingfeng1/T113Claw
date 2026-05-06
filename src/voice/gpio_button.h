#pragma once
/*
 * GPIO Button Detection
 *
 * Uses Linux sysfs GPIO interface for button press detection.
 * Runs a background polling thread.
 */

/* Callback on button press/release (edge) */
typedef void (*gpio_button_cb_t)(int pressed, void *user);

/*
 * Start button detection on a GPIO pin.
 * gpio_num:   Linux GPIO number (e.g., 106 for PD10)
 * cb:         button event callback
 * user:       callback context
 *
 * Returns 0 on success, -1 on failure.
 */
int gpio_button_start(int gpio_num, gpio_button_cb_t cb, void *user);

/* Stop button detection and release GPIO */
void gpio_button_stop(void);

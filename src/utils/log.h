#pragma once
/*
 * T113Claw Logging Macros
 *
 * Usage:
 *   LOG_I("module", "message %d", value);
 *   LOG_E("module", "error: %s", strerror(errno));
 */

#include <stdio.h>
#include <time.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} log_level_t;

#if defined(__GNUC__)
extern void mc_log_error_sink(const char *tag, const char *message) __attribute__((weak));
#else
extern void mc_log_error_sink(const char *tag, const char *message);
#endif

/* Global log level — can be changed at runtime */
extern log_level_t g_log_level;

#define _LOG(level, tag, color, fmt, ...) do { \
    if ((level) >= g_log_level) { \
        time_t _t = time(NULL); \
        struct tm _tm; \
        char _mc_log_msg[256]; \
        localtime_r(&_t, &_tm); \
        snprintf(_mc_log_msg, sizeof(_mc_log_msg), fmt, ##__VA_ARGS__); \
        fprintf(stderr, color "%02d:%02d:%02d [%s] %s: %s\033[0m\n", \
                _tm.tm_hour, _tm.tm_min, _tm.tm_sec, \
                #level + 10, tag, _mc_log_msg); \
        if ((level) == LOG_LEVEL_ERROR && mc_log_error_sink) { \
            mc_log_error_sink(tag, _mc_log_msg); \
        } \
    } \
} while(0)

#define LOG_D(tag, fmt, ...) _LOG(LOG_LEVEL_DEBUG, tag, "\033[0;37m", fmt, ##__VA_ARGS__)
#define LOG_I(tag, fmt, ...) _LOG(LOG_LEVEL_INFO,  tag, "\033[0;32m", fmt, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...) _LOG(LOG_LEVEL_WARN,  tag, "\033[0;33m", fmt, ##__VA_ARGS__)
#define LOG_E(tag, fmt, ...) _LOG(LOG_LEVEL_ERROR, tag, "\033[0;31m", fmt, ##__VA_ARGS__)

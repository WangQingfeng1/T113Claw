/*
 * Tool: get_current_time
 */

#include "tool_registry.h"
#include <stdio.h>
#include <time.h>

int tool_time_execute(const char *input, char *output, size_t size)
{
    (void)input;

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    char tz_name[32] = "";
    strftime(tz_name, sizeof(tz_name), "%Z", &tm);

    snprintf(output, size,
             "Current time: %04d-%02d-%02d %02d:%02d:%02d %s (unix: %ld)",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec,
             tz_name, (long)now);

    return 0;
}

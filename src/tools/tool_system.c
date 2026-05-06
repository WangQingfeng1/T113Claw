/*
 * Tool: system_info
 */

#include "tool_registry.h"
#include <stdio.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>

int tool_system_execute(const char *input, char *output, size_t size)
{
    (void)input;
    size_t off = 0;

    /* Uptime & memory */
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        long uptime_h = si.uptime / 3600;
        long uptime_m = (si.uptime % 3600) / 60;
        off += snprintf(output + off, size - off,
                        "Uptime: %ldh %ldm\n"
                        "Total RAM: %lu MB\n"
                        "Free RAM: %lu MB\n"
                        "Load (1m): %.2f\n",
                        uptime_h, uptime_m,
                        (unsigned long)(si.totalram * si.mem_unit / (1024 * 1024)),
                        (unsigned long)(si.freeram * si.mem_unit / (1024 * 1024)),
                        si.loads[0] / 65536.0);
    }

    /* CPU temperature */
    FILE *f = fopen("/sys/devices/virtual/thermal/thermal_zone0/temp", "r");
    if (f) {
        int temp = 0;
        if (fscanf(f, "%d", &temp) == 1) {
            off += snprintf(output + off, size - off,
                            "CPU temp: %.1f°C\n", temp / 1000.0);
        }
        fclose(f);
    }

    /* Storage */
    struct statvfs sv;
    if (statvfs("/", &sv) == 0) {
        unsigned long total_mb = (sv.f_blocks * sv.f_frsize) / (1024 * 1024);
        unsigned long free_mb = (sv.f_bfree * sv.f_frsize) / (1024 * 1024);
        off += snprintf(output + off, size - off,
                        "Storage: %lu MB total, %lu MB free\n",
                        total_mb, free_mb);
    }

    return 0;
}

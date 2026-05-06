#pragma once
/*
 * T113Claw Utility Helpers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <errno.h>

/* Return codes */
#define MC_OK          0
#define MC_ERR        -1
#define MC_ERR_NOMEM  -2
#define MC_ERR_TIMEOUT -3
#define MC_ERR_NOTFOUND -4
#define MC_ERR_INVALID -5

/* Safe string duplication */
static inline char *mc_strdup(const char *s)
{
    return s ? strdup(s) : NULL;
}

/* Ensure a directory exists (mkdir -p one level) */
static inline int mc_ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return 0;
    return mkdir(path, 0755);
}

/* Safe snprintf that returns the buffer pointer */
static inline char *mc_snprintf(char *buf, size_t size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static inline char *mc_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return buf;
}

/* Read entire file into heap-allocated buffer. Caller must free. */
static inline char *mc_read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) {
        fclose(f);
        if (out_len) *out_len = 0;
        return calloc(1, 1);
    }

    char *buf = malloc(len + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t rd = fread(buf, 1, len, f);
    buf[rd] = '\0';
    fclose(f);

    if (out_len) *out_len = rd;
    return buf;
}

/* Write string to file (overwrite) */
static inline int mc_write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (!f) return MC_ERR;
    fputs(content, f);
    fclose(f);
    return MC_OK;
}

/* Append string to file */
static inline int mc_append_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "a");
    if (!f) return MC_ERR;
    fputs(content, f);
    fclose(f);
    return MC_OK;
}

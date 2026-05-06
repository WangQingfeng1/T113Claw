#pragma once
/*
 * T113Claw HTTP Client — libcurl wrapper
 */

#include <stddef.h>

/* Dynamic response buffer */
typedef struct {
    char   *data;
    size_t  size;
    long    http_code;
} http_response_t;

/* Initialize HTTP client (call once at startup) */
int http_client_init(void);

/* Cleanup HTTP client */
void http_client_cleanup(void);

/* Perform HTTP POST with JSON body.
 * Headers:  NULL-terminated array of "Key: Value" strings, or NULL.
 * Response: caller must call http_response_free() when done.
 */
int http_post_json(const char *url, const char *json_body,
                   const char **headers, http_response_t *resp);

/* Perform HTTP GET.
 * Response: caller must call http_response_free() when done.
 */
int http_get(const char *url, const char **headers, http_response_t *resp);

/* Free response data */
void http_response_free(http_response_t *resp);

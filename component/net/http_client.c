#include "http_client.h"
#include "utils/log.h"
#include "utils/utils.h"
#include "config/config.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <curl/curl.h>

#define TAG "http"

static int s_initialized = 0;

/* ── Write callback for curl ─────────────────────────────── */

static size_t write_cb(void *data, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    http_response_t *resp = (http_response_t *)userp;

    char *ptr = realloc(resp->data, resp->size + realsize + 1);
    if (!ptr) return 0;

    resp->data = ptr;
    memcpy(resp->data + resp->size, data, realsize);
    resp->size += realsize;
    resp->data[resp->size] = '\0';

    return realsize;
}

/* ── Internal request helper ─────────────────────────────── */

static int do_request(const char *url, const char *method,
                      const char *body, const char **headers,
                      http_response_t *resp)
{
    CURL *curl = curl_easy_init();
    if (!curl) return MC_ERR;

    memset(resp, 0, sizeof(*resp));

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    /* libcurl signals are unsafe in multi-threaded programs and can
     * crash unrelated threads when DNS/connect timeouts fire. */
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    /* Proxy */
    const char *proxy = config_get_proxy_url();
    if (proxy) {
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy);
    }

    /* Headers */
    struct curl_slist *hdr_list = NULL;
    if (headers) {
        for (int i = 0; headers[i]; i++) {
            hdr_list = curl_slist_append(hdr_list, headers[i]);
        }
    }

    if (body) {
        /* Only add Content-Type if not already provided by caller */
        bool has_content_type = false;
        if (headers) {
            for (int j = 0; headers[j]; j++) {
                if (strncasecmp(headers[j], "Content-Type:", 13) == 0) {
                    has_content_type = true;
                    break;
                }
            }
        }
        if (!has_content_type) {
            hdr_list = curl_slist_append(hdr_list, "Content-Type: application/json");
        }
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    }

    if (hdr_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr_list);
    }

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp->http_code);
    } else {
        LOG_E(TAG, "curl error: %s (url: %s)", curl_easy_strerror(res), url);
    }

    curl_slist_free_all(hdr_list);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK) ? MC_OK : MC_ERR;
}

/* ── Public API ───────────────────────────────────────────── */

int http_client_init(void)
{
    if (!s_initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        s_initialized = 1;
        LOG_I(TAG, "HTTP client initialized");
    }
    return MC_OK;
}

void http_client_cleanup(void)
{
    if (s_initialized) {
        curl_global_cleanup();
        s_initialized = 0;
    }
}

int http_post_json(const char *url, const char *json_body,
                   const char **headers, http_response_t *resp)
{
    return do_request(url, "POST", json_body, headers, resp);
}

int http_get(const char *url, const char **headers, http_response_t *resp)
{
    return do_request(url, "GET", NULL, headers, resp);
}

void http_response_free(http_response_t *resp)
{
    if (resp) {
        free(resp->data);
        resp->data = NULL;
        resp->size = 0;
    }
}

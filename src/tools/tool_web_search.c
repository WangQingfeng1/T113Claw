#include "tool_web_search.h"
#include "config/config.h"
#include "http_client.h"
#include "t113claw_config.h"
#include "utils/log.h"
#include "utils/utils.h"

#include <cJSON.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define TAG "tool_search"

#define SEARCH_RESULT_TITLE_LEN   256
#define SEARCH_RESULT_URL_LEN     512
#define SEARCH_RESULT_SUMMARY_LEN 512
#define SEARCH_RESULT_SOURCE_LEN  128
#define SEARCH_RESULT_DATE_LEN    64

typedef enum {
    SEARCH_MODE_AUTO = 0,
    SEARCH_MODE_SOGOU,
    SEARCH_MODE_TAVILY,
} search_mode_t;

typedef enum {
    SEARCH_PROVIDER_SOGOU = 0,
    SEARCH_PROVIDER_TAVILY,
} search_provider_t;

typedef struct {
    char query[T113CLAW_SEARCH_MAX_QUERY_LEN];
    char site[T113CLAW_SEARCH_MAX_SITE_LEN];
    int max_results;
} search_request_t;

typedef struct {
    char title[SEARCH_RESULT_TITLE_LEN];
    char url[SEARCH_RESULT_URL_LEN];
    char summary[SEARCH_RESULT_SUMMARY_LEN];
    char source[SEARCH_RESULT_SOURCE_LEN];
    char date[SEARCH_RESULT_DATE_LEN];
} search_result_t;

static int appendf(char *output, size_t output_size, size_t *off,
                   const char *fmt, ...)
{
    if (*off >= output_size) {
        return -1;
    }

    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(output + *off, output_size - *off, fmt, ap);
    va_end(ap);

    if (written < 0) {
        return -1;
    }

    if ((size_t)written >= output_size - *off) {
        *off = output_size - 1;
        return -1;
    }

    *off += (size_t)written;
    return 0;
}

static int decode_html_entity(const char *src, char *out, size_t *consumed)
{
    struct entity_map {
        const char *name;
        char value;
    } map[] = {
        {"&nbsp;", ' '},
        {"&amp;", '&'},
        {"&quot;", '"'},
        {"&#39;", '\''},
        {"&apos;", '\''},
        {"&lt;", '<'},
        {"&gt;", '>'},
    };

    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        size_t len = strlen(map[i].name);
        if (strncmp(src, map[i].name, len) == 0) {
            *out = map[i].value;
            *consumed = len;
            return 1;
        }
    }

    return 0;
}

static void normalize_text(char *text)
{
    char *src = text;
    char *dst = text;
    int in_tag = 0;
    int last_space = 1;

    while (*src) {
        if (*src == '<') {
            in_tag = 1;
            src++;
            continue;
        }
        if (in_tag) {
            if (*src == '>') {
                in_tag = 0;
            }
            src++;
            continue;
        }

        char ch;
        size_t consumed = 0;
        if (*src == '&' && decode_html_entity(src, &ch, &consumed)) {
            src += consumed;
        } else {
            ch = *src++;
        }

        if (ch == '\r' || ch == '\n' || ch == '\t') {
            ch = ' ';
        }

        if (isspace((unsigned char)ch)) {
            if (last_space) {
                continue;
            }
            ch = ' ';
            last_space = 1;
        } else {
            last_space = 0;
        }

        *dst++ = ch;
    }

    while (dst > text && dst[-1] == ' ') {
        dst--;
    }
    *dst = '\0';
}

static void utf8_truncate_inplace(char *text, size_t max_bytes)
{
    size_t len = strlen(text);
    if (len <= max_bytes) {
        return;
    }

    size_t cut = max_bytes;
    while (cut > 0 && ((unsigned char)text[cut] & 0xC0) == 0x80) {
        cut--;
    }
    if (cut == 0) {
        cut = max_bytes;
    }
    text[cut] = '\0';
}

static void shorten_summary(char *summary)
{
    size_t len = strlen(summary);
    if (len <= 220) {
        return;
    }

    utf8_truncate_inplace(summary, 216);
    size_t off = strlen(summary);
    if (off + 3 < SEARCH_RESULT_SUMMARY_LEN) {
        memcpy(summary + off, "...", 4);
    }
}

static int url_encode(const char *src, char *dst, size_t dst_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t off = 0;

    for (; *src && off + 4 < dst_size; src++) {
        unsigned char ch = (unsigned char)*src;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
            ch == '.' || ch == '~') {
            dst[off++] = (char)ch;
        } else if (ch == ' ') {
            dst[off++] = '%';
            dst[off++] = '2';
            dst[off++] = '0';
        } else {
            dst[off++] = '%';
            dst[off++] = hex[(ch >> 4) & 0x0F];
            dst[off++] = hex[ch & 0x0F];
        }
    }

    dst[off] = '\0';
    return (int)off;
}

static int config_bool(const char *section, const char *key, const char *def)
{
    const char *value = config_get_default(section, key, def);
    if (!value || !value[0]) {
        return 0;
    }

    return strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0 ||
           strcasecmp(value, "yes") == 0 || strcasecmp(value, "on") == 0;
}

static search_mode_t parse_search_mode(const char *mode)
{
    if (!mode || !mode[0] || strcasecmp(mode, "auto") == 0) {
        return SEARCH_MODE_AUTO;
    }
    if (strcasecmp(mode, "tavily") == 0) {
        return SEARCH_MODE_TAVILY;
    }
    return SEARCH_MODE_SOGOU;
}

static const char *provider_name(search_provider_t provider)
{
    return provider == SEARCH_PROVIDER_TAVILY ? "tavily" : "sogou";
}

static search_provider_t resolve_provider(search_mode_t *mode_out)
{
    const char *mode_value =
        config_get_default("search", "provider", T113CLAW_SEARCH_PROVIDER_DEFAULT);
    const char *tavily_key =
        config_get_default("search", "tavily_api_key", T113CLAW_SECRET_TAVILY_API_KEY);
    int domestic_only = config_bool("search", "domestic_only",
                                    T113CLAW_SEARCH_DOMESTIC_ONLY_DEFAULT);
    search_mode_t mode = parse_search_mode(mode_value);

    if (mode_out) {
        *mode_out = mode;
    }

    if (mode == SEARCH_MODE_TAVILY) {
        return SEARCH_PROVIDER_TAVILY;
    }
    if (mode == SEARCH_MODE_SOGOU) {
        return SEARCH_PROVIDER_SOGOU;
    }

    if (domestic_only || !tavily_key || !tavily_key[0]) {
        return SEARCH_PROVIDER_SOGOU;
    }
    return SEARCH_PROVIDER_TAVILY;
}

static int copy_json_string(cJSON *root, const char *key, char *buf, size_t size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || !item->valuestring || !item->valuestring[0]) {
        return 0;
    }

    snprintf(buf, size, "%s", item->valuestring);
    return 1;
}

static int copy_first_json_string(cJSON *root, const char *keys[], size_t key_count,
                                  char *buf, size_t size)
{
    for (size_t i = 0; i < key_count; i++) {
        if (copy_json_string(root, keys[i], buf, size)) {
            return 1;
        }
    }
    return 0;
}

static void derive_source_from_url(const char *url, char *source, size_t size)
{
    if (source[0] || !url || !url[0]) {
        return;
    }

    const char *host = strstr(url, "://");
    host = host ? host + 3 : url;
    size_t off = 0;
    while (host[off] && host[off] != '/' && host[off] != '?' && host[off] != '#' &&
           off + 1 < size) {
        source[off] = host[off];
        off++;
    }
    source[off] = '\0';
}

static int result_seen(search_result_t *results, int count, const char *url)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(results[i].url, url) == 0) {
            return 1;
        }
    }
    return 0;
}

static void normalize_result(search_result_t *result)
{
    normalize_text(result->title);
    normalize_text(result->summary);
    normalize_text(result->source);
    normalize_text(result->date);
    shorten_summary(result->summary);
    derive_source_from_url(result->url, result->source, sizeof(result->source));
}

static int extract_sogou_summary(cJSON *root, char *summary, size_t size)
{
    const char *summary_keys[] = {"content", "summary"};
    if (copy_first_json_string(root, summary_keys,
                               sizeof(summary_keys) / sizeof(summary_keys[0]),
                               summary, size)) {
        return 1;
    }

    cJSON *right_text_arr = cJSON_GetObjectItemCaseSensitive(root, "rightTextArr");
    if (!cJSON_IsArray(right_text_arr)) {
        return 0;
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, right_text_arr) {
        cJSON *content = cJSON_GetObjectItemCaseSensitive(item, "content");
        if (cJSON_IsString(content) && content->valuestring && content->valuestring[0]) {
            snprintf(summary, size, "%s", content->valuestring);
            return 1;
        }
    }

    return 0;
}

static int parse_sogou_script_result(cJSON *root, search_result_t *result)
{
    const char *url_keys[] = {"url", "h5Url", "urlEncrypt"};
    const char *source_keys[] = {"showName", "authorName", "source", "siteName"};
    const char *date_keys[] = {"date", "urlDate", "uploadTime"};

    memset(result, 0, sizeof(*result));

    if (!copy_json_string(root, "title", result->title, sizeof(result->title))) {
        return 0;
    }
    if (!copy_first_json_string(root, url_keys, sizeof(url_keys) / sizeof(url_keys[0]),
                                result->url, sizeof(result->url))) {
        return 0;
    }

    extract_sogou_summary(root, result->summary, sizeof(result->summary));
    copy_first_json_string(root, source_keys, sizeof(source_keys) / sizeof(source_keys[0]),
                           result->source, sizeof(result->source));
    copy_first_json_string(root, date_keys, sizeof(date_keys) / sizeof(date_keys[0]),
                           result->date, sizeof(result->date));

    normalize_result(result);

    return result->title[0] && result->url[0] &&
           (strncmp(result->url, "http://", 7) == 0 ||
            strncmp(result->url, "https://", 8) == 0);
}

static int collect_sogou_results(const char *html, search_result_t *results,
                                 int max_results)
{
    const char *cursor = html;
    int count = 0;
    const char *needle = "<script id=\"data-";
    const char *type_needle = "type=\"application/json\">";
    const char *end_needle = "</script>";

    while ((cursor = strstr(cursor, needle)) != NULL && count < max_results) {
        const char *json_start = strstr(cursor, type_needle);
        if (!json_start) {
            cursor += strlen(needle);
            continue;
        }
        json_start += strlen(type_needle);

        const char *json_end = strstr(json_start, end_needle);
        if (!json_end) {
            break;
        }

        size_t json_len = (size_t)(json_end - json_start);
        if (json_len == 0 || json_len > 32768) {
            cursor = json_end + strlen(end_needle);
            continue;
        }

        char *json_buf = malloc(json_len + 1);
        if (!json_buf) {
            break;
        }

        memcpy(json_buf, json_start, json_len);
        json_buf[json_len] = '\0';

        cJSON *root = cJSON_Parse(json_buf);
        free(json_buf);

        if (root) {
            search_result_t candidate;
            if (parse_sogou_script_result(root, &candidate) &&
                !result_seen(results, count, candidate.url)) {
                results[count++] = candidate;
            }
            cJSON_Delete(root);
        }

        cursor = json_end + strlen(end_needle);
    }

    return count;
}

static int collect_tavily_results(cJSON *root, search_result_t *results, int max_results)
{
    cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "results");
    if (!cJSON_IsArray(items)) {
        return 0;
    }

    int count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, items) {
        if (count >= max_results) {
            break;
        }

        search_result_t *result = &results[count];
        memset(result, 0, sizeof(*result));

        copy_json_string(item, "title", result->title, sizeof(result->title));
        copy_json_string(item, "url", result->url, sizeof(result->url));
        copy_json_string(item, "content", result->summary, sizeof(result->summary));
        copy_json_string(item, "published_date", result->date, sizeof(result->date));
        normalize_result(result);

        if (!result->title[0] || !result->url[0] || result_seen(results, count, result->url)) {
            continue;
        }

        count++;
    }

    return count;
}

static void format_results(const search_request_t *request, search_provider_t provider,
                           const char *note, search_result_t *results, int count,
                           char *output, size_t output_size)
{
    size_t off = 0;
    appendf(output, output_size, &off,
            "Web search results for: %s\nProvider: %s\n",
            request->query, provider_name(provider));

    if (request->site[0]) {
        appendf(output, output_size, &off, "Site filter: %s\n", request->site);
    }
    if (note && note[0]) {
        appendf(output, output_size, &off, "Note: %s\n", note);
    }
    appendf(output, output_size, &off, "\n");

    if (count <= 0) {
        appendf(output, output_size, &off,
                "No web results found. Try rephrasing the query with more specific keywords.\n");
        return;
    }

    for (int i = 0; i < count; i++) {
        appendf(output, output_size, &off, "%d. %s\n", i + 1, results[i].title);
        if (results[i].source[0] || results[i].date[0]) {
            appendf(output, output_size, &off, "   Source: %s%s%s\n",
                    results[i].source[0] ? results[i].source : "(unknown)",
                    results[i].date[0] ? " | Date: " : "",
                    results[i].date[0] ? results[i].date : "");
        }
        appendf(output, output_size, &off, "   URL: %s\n", results[i].url);
        if (results[i].summary[0]) {
            appendf(output, output_size, &off, "   Summary: %s\n", results[i].summary);
        }
        appendf(output, output_size, &off, "\n");
    }
}

static int parse_request(const char *input, search_request_t *request,
                         char *output, size_t output_size)
{
    memset(request, 0, sizeof(*request));
    request->max_results = T113CLAW_SEARCH_MAX_RESULTS;

    cJSON *root = cJSON_Parse(input);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return MC_ERR_INVALID;
    }

    cJSON *query = cJSON_GetObjectItemCaseSensitive(root, "query");
    if (!cJSON_IsString(query) || !query->valuestring || !query->valuestring[0]) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: missing 'query'");
        return MC_ERR_INVALID;
    }

    snprintf(request->query, sizeof(request->query), "%s", query->valuestring);

    cJSON *site = cJSON_GetObjectItemCaseSensitive(root, "site");
    if (cJSON_IsString(site) && site->valuestring && site->valuestring[0]) {
        snprintf(request->site, sizeof(request->site), "%s", site->valuestring);
    }

    cJSON *max_results = cJSON_GetObjectItemCaseSensitive(root, "max_results");
    if (cJSON_IsNumber(max_results)) {
        request->max_results = max_results->valueint;
    }

    if (request->max_results <= 0) {
        request->max_results = T113CLAW_SEARCH_MAX_RESULTS;
    }
    if (request->max_results > 8) {
        request->max_results = 8;
    }

    cJSON_Delete(root);
    return MC_OK;
}

static char *build_tavily_payload(const search_request_t *request)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    cJSON_AddStringToObject(root, "query", request->query);
    cJSON_AddNumberToObject(root, "max_results", request->max_results);
    cJSON_AddBoolToObject(root, "include_answer", 0);
    cJSON_AddStringToObject(root, "search_depth", "basic");
    cJSON_AddStringToObject(root, "topic", "general");

    if (request->site[0]) {
        cJSON *domains = cJSON_AddArrayToObject(root, "include_domains");
        if (domains) {
            cJSON_AddItemToArray(domains, cJSON_CreateString(request->site));
        }
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return payload;
}

static int search_tavily(const search_request_t *request, search_result_t *results,
                         char *output, size_t output_size)
{
    const char *api_key =
        config_get_default("search", "tavily_api_key", T113CLAW_SECRET_TAVILY_API_KEY);
    if (!api_key || !api_key[0]) {
        snprintf(output, output_size,
                 "Error: Tavily API key not configured. Set search.tavily_api_key or T113CLAW_SECRET_TAVILY_API_KEY.");
        return MC_ERR_INVALID;
    }

    char *payload = build_tavily_payload(request);
    if (!payload) {
        snprintf(output, output_size, "Error: failed to build Tavily request");
        return MC_ERR_NOMEM;
    }

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    const char *headers[] = {
        "Accept: application/json",
        "Content-Type: application/json",
        auth_header,
        NULL,
    };

    http_response_t resp;
    int rc = http_post_json(T113CLAW_SEARCH_TAVILY_URL, payload, headers, &resp);
    free(payload);

    if (rc != MC_OK) {
        snprintf(output, output_size, "Error: Tavily request failed");
        return rc;
    }

    if (resp.http_code != 200 || !resp.data) {
        snprintf(output, output_size, "Error: Tavily HTTP %ld", resp.http_code);
        http_response_free(&resp);
        return MC_ERR;
    }

    cJSON *root = cJSON_Parse(resp.data);
    http_response_free(&resp);
    if (!root) {
        snprintf(output, output_size, "Error: failed to parse Tavily response");
        return MC_ERR;
    }

    int count = collect_tavily_results(root, results, request->max_results);
    cJSON_Delete(root);
    return count;
}

static int search_sogou(const search_request_t *request, search_result_t *results,
                        char *output, size_t output_size)
{
    char query_buf[T113CLAW_SEARCH_MAX_QUERY_LEN + T113CLAW_SEARCH_MAX_SITE_LEN + 16];
    if (request->site[0]) {
        snprintf(query_buf, sizeof(query_buf), "site:%s %s", request->site, request->query);
    } else {
        snprintf(query_buf, sizeof(query_buf), "%s", request->query);
    }

    char encoded_query[1024];
    url_encode(query_buf, encoded_query, sizeof(encoded_query));

    char url[1280];
    snprintf(url, sizeof(url), "%s?keyword=%s", T113CLAW_SEARCH_SOGOU_URL, encoded_query);

    const char *headers[] = {
        "User-Agent: Mozilla/5.0",
        "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
        "Accept-Language: zh-CN,zh;q=0.9",
        NULL,
    };

    http_response_t resp;
    int rc = http_get(url, headers, &resp);
    if (rc != MC_OK) {
        snprintf(output, output_size, "Error: Sogou search request failed");
        return rc;
    }

    if (resp.http_code != 200 || !resp.data) {
        snprintf(output, output_size, "Error: Sogou HTTP %ld", resp.http_code);
        http_response_free(&resp);
        return MC_ERR;
    }

    int count = collect_sogou_results(resp.data, results, request->max_results);
    http_response_free(&resp);
    return count;
}

int tool_web_search_execute(const char *input, char *output, size_t output_size)
{
    search_request_t request;
    int rc = parse_request(input, &request, output, output_size);
    if (rc != MC_OK) {
        return rc;
    }

    search_result_t results[8];
    memset(results, 0, sizeof(results));

    search_mode_t mode = SEARCH_MODE_AUTO;
    search_provider_t provider = resolve_provider(&mode);
    const char *note = NULL;

    LOG_I(TAG, "Searching provider=%s query=%.120s", provider_name(provider), request.query);

    if (provider == SEARCH_PROVIDER_TAVILY) {
        rc = search_tavily(&request, results, output, output_size);
        if (rc < 0 && mode == SEARCH_MODE_AUTO) {
            LOG_W(TAG, "Tavily unavailable, falling back to Sogou");
            memset(results, 0, sizeof(results));
            rc = search_sogou(&request, results, output, output_size);
            provider = SEARCH_PROVIDER_SOGOU;
            note = "Tavily request failed, fell back to Sogou mobile web search.";
        }
    } else {
        rc = search_sogou(&request, results, output, output_size);
    }

    if (rc < 0) {
        return rc;
    }

    format_results(&request, provider, note, results, rc, output, output_size);
    LOG_I(TAG, "Search complete: provider=%s results=%d", provider_name(provider), rc);
    return MC_OK;
}
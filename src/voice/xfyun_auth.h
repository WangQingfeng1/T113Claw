#pragma once
/*
 * iFlytek (讯飞) WebSocket API Authentication
 *
 * Generates signed URLs for STT/TTS WebSocket endpoints using
 * HMAC-SHA256 + base64 per iFlytek's auth protocol.
 */

#include <stddef.h>

/*
 * Build an authenticated WebSocket URL.
 *
 * base_url:   e.g. "wss://iat-api.xfyun.cn/v2/iat"
 * api_key:    from iFlytek console
 * api_secret: from iFlytek console
 * out_buf:    buffer to receive the full URL with auth params
 * buf_size:   size of out_buf (recommend 1024+)
 *
 * Returns 0 on success, -1 on failure.
 */
int xfyun_build_auth_url(const char *base_url,
                          const char *api_key,
                          const char *api_secret,
                          char *out_buf, size_t buf_size);

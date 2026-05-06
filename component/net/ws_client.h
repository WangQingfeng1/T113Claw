#pragma once
/*
 * Minimal WebSocket Client over TLS (OpenSSL)
 *
 * Supports binary frames (opcode 0x02) needed by Feishu protobuf protocol.
 * Client-only, single-threaded (use from one thread).
 */

#include <stddef.h>
#include <stdint.h>

typedef struct ws_client ws_client_t;

/* Connect to a wss:// URL. Returns NULL on failure. */
ws_client_t *ws_connect(const char *url);

/* Send a text frame (opcode 0x01). Returns 0 on success, -1 on error. */
int ws_send_text(ws_client_t *ws, const char *text, size_t len);

/* Send a binary frame (opcode 0x02). Returns 0 on success, -1 on error. */
int ws_send_binary(ws_client_t *ws, const uint8_t *data, size_t len);

/* Receive a frame.
 * out_buf:  caller-provided buffer (will be filled with payload)
 * buf_size: size of out_buf
 * out_len:  actual payload length written
 * out_opcode: WebSocket opcode (0x01=text, 0x02=binary, 0x08=close, 0x09=ping, 0x0A=pong)
 * timeout_ms: 0 = non-blocking, >0 = wait up to timeout_ms
 * Returns 0 on success, -1 on error, 1 on timeout (no data). */
int ws_recv(ws_client_t *ws, uint8_t *out_buf, size_t buf_size,
            size_t *out_len, uint8_t *out_opcode, int timeout_ms);

/* Check if connected */
int ws_is_connected(ws_client_t *ws);

/* Close and free */
void ws_close(ws_client_t *ws);

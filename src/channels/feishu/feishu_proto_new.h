#pragma once
/*
 * Feishu WebSocket Protobuf Frame Codec
 *
 * Feishu WS uses binary frames with a custom protobuf encoding:
 *   Field 1 (varint): seq_id
 *   Field 2 (varint): log_id
 *   Field 3 (varint): service
 *   Field 4 (varint): method (0=control, non-0=event)
 *   Field 5 (bytes):  headers (repeated, sub-message with key/value strings)
 *   Field 8 (bytes):  payload (JSON)
 */

#include <stdint.h>
#include <stddef.h>

#define FEISHU_MAX_HEADERS 16

typedef struct {
    char key[32];
    char value[128];
} feishu_header_t;

typedef struct {
    uint64_t        seq_id;
    uint64_t        log_id;
    int32_t         service;
    int32_t         method;
    feishu_header_t headers[FEISHU_MAX_HEADERS];
    size_t          header_count;
    const uint8_t  *payload;      /* points into input buffer (not owned) */
    size_t          payload_len;
} feishu_frame_t;

/* Parse a protobuf-encoded WebSocket binary frame.
 * Returns 0 on success, -1 on parse error.
 * payload points into buf (valid only while buf alive). */
int feishu_proto_parse_frame(const uint8_t *buf, size_t len, feishu_frame_t *frame);

/* Get a header value by key. Returns NULL if not found. */
const char *feishu_frame_header(const feishu_frame_t *frame, const char *key);

/* Build a protobuf frame into out_buf.
 * Copies the frame metadata + overwrites payload with given data.
 * Returns bytes written, or -1 on error. */
int feishu_proto_build_frame(const feishu_frame_t *frame,
                             const uint8_t *payload, size_t payload_len,
                             uint8_t *out_buf, size_t out_cap);

/* Build a ping frame for the given service_id.
 * Returns bytes written, or -1 on error. */
int feishu_proto_build_ping(int32_t service_id,
                            uint8_t *out_buf, size_t out_cap);

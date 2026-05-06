/*
 * Feishu WebSocket Protobuf Frame Codec
 *
 * Based on mimiclaw reference implementation.
 * Implements encode/decode for Feishu's custom protobuf WS framing.
 */

#include "feishu_proto.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ── Varint read/write ────────────────────────────────────── */

static bool read_varint(const uint8_t *buf, size_t len, size_t *pos, uint64_t *out)
{
    uint64_t v = 0;
    int shift = 0;
    while (*pos < len && shift <= 63) {
        uint8_t b = buf[(*pos)++];
        v |= ((uint64_t)(b & 0x7F)) << shift;
        if ((b & 0x80) == 0) {
            *out = v;
            return true;
        }
        shift += 7;
    }
    return false;
}

static bool write_varint(uint8_t *buf, size_t cap, size_t *pos, uint64_t value)
{
    do {
        if (*pos >= cap) return false;
        uint8_t byte = (uint8_t)(value & 0x7F);
        value >>= 7;
        if (value) byte |= 0x80;
        buf[(*pos)++] = byte;
    } while (value);
    return true;
}

static bool write_tag(uint8_t *buf, size_t cap, size_t *pos, uint32_t field, uint8_t wire_type)
{
    return write_varint(buf, cap, pos, ((uint64_t)field << 3) | wire_type);
}

static bool write_bytes(uint8_t *buf, size_t cap, size_t *pos,
                        uint32_t field, const uint8_t *data, size_t len)
{
    if (!write_tag(buf, cap, pos, field, 2)) return false;
    if (!write_varint(buf, cap, pos, len)) return false;
    if (*pos + len > cap) return false;
    memcpy(buf + *pos, data, len);
    *pos += len;
    return true;
}

static bool write_string(uint8_t *buf, size_t cap, size_t *pos,
                         uint32_t field, const char *s)
{
    return write_bytes(buf, cap, pos, field, (const uint8_t *)s, strlen(s));
}

/* ── Skip unknown fields ──────────────────────────────────── */

static bool skip_field(const uint8_t *buf, size_t len, size_t *pos, uint8_t wire_type)
{
    uint64_t n = 0;
    switch (wire_type) {
    case 0: return read_varint(buf, len, pos, &n);
    case 1: if (*pos + 8 > len) return false; *pos += 8; return true;
    case 2:
        if (!read_varint(buf, len, pos, &n)) return false;
        if (*pos + (size_t)n > len) return false;
        *pos += (size_t)n;
        return true;


    case 5: if (*pos + 4 > len) return false; *pos += 4; return true;
    default: return false;
    }
}

/* ── Parse header sub-message ─────────────────────────────── */

static bool parse_header(const uint8_t *buf, size_t len, feishu_header_t *h)
{
    memset(h, 0, sizeof(*h));
    size_t pos = 0;

    while (pos < len) {
        uint64_t tag = 0, slen = 0;
        if (!read_varint(buf, len, &pos, &tag)) return false;
        uint32_t field = (uint32_t)(tag >> 3);
        uint8_t wt = (uint8_t)(tag & 0x07);

        if (wt != 2) {
            if (!skip_field(buf, len, &pos, wt)) return false;
            continue;
        }
        if (!read_varint(buf, len, &pos, &slen)) return false;
        if (pos + (size_t)slen > len) return false;

        if (field == 1) {
            size_t n = (slen < sizeof(h->key) - 1) ? (size_t)slen : sizeof(h->key) - 1;
            memcpy(h->key, buf + pos, n);
            h->key[n] = '\0';
        } else if (field == 2) {
            size_t n = (slen < sizeof(h->value) - 1) ? (size_t)slen : sizeof(h->value) - 1;
            memcpy(h->value, buf + pos, n);
            h->value[n] = '\0';
        }
        pos += (size_t)slen;
    }
    return true;
}

/* ── Parse frame ──────────────────────────────────────────── */

int feishu_proto_parse_frame(const uint8_t *buf, size_t len, feishu_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    size_t pos = 0;

    while (pos < len) {
        uint64_t tag = 0, v = 0, blen = 0;
        if (!read_varint(buf, len, &pos, &tag)) return -1;
        uint32_t field = (uint32_t)(tag >> 3);
        uint8_t wt = (uint8_t)(tag & 0x07);

        if (field == 1 && wt == 0) {
            if (!read_varint(buf, len, &pos, &frame->seq_id)) return -1;
        } else if (field == 2 && wt == 0) {
            if (!read_varint(buf, len, &pos, &frame->log_id)) return -1;
        } else if (field == 3 && wt == 0) {
            if (!read_varint(buf, len, &pos, &v)) return -1;
            frame->service = (int32_t)v;
        } else if (field == 4 && wt == 0) {
            if (!read_varint(buf, len, &pos, &v)) return -1;
            frame->method = (int32_t)v;
        } else if (field == 5 && wt == 2) {
            if (!read_varint(buf, len, &pos, &blen)) return -1;
            if (pos + (size_t)blen > len) return -1;
            if (frame->header_count < FEISHU_MAX_HEADERS) {
                parse_header(buf + pos, (size_t)blen,
                             &frame->headers[frame->header_count++]);
            }
            pos += (size_t)blen;
        } else if (field == 8 && wt == 2) {
            if (!read_varint(buf, len, &pos, &blen)) return -1;
            if (pos + (size_t)blen > len) return -1;
            frame->payload = buf + pos;
            frame->payload_len = (size_t)blen;
            pos += (size_t)blen;
        } else {
            if (!skip_field(buf, len, &pos, wt)) return -1;
        }
    }

    return 0;
}

const char *feishu_frame_header(const feishu_frame_t *frame, const char *key)
{
    for (size_t i = 0; i < frame->header_count; i++) {
        if (strcmp(frame->headers[i].key, key) == 0)
            return frame->headers[i].value;
    }
    return NULL;
}

/* ── Build frame ──────────────────────────────────────────── */

static bool encode_header(uint8_t *buf, size_t cap, size_t *len,
                          const char *key, const char *value)
{
    size_t pos = 0;
    if (!write_string(buf, cap, &pos, 1, key)) return false;
    if (!write_string(buf, cap, &pos, 2, value)) return false;
    *len = pos;
    return true;
}

int feishu_proto_build_frame(const feishu_frame_t *frame,
                             const uint8_t *payload, size_t payload_len,
                             uint8_t *out_buf, size_t out_cap)
{
    size_t pos = 0;

    /* Field 1: seq_id */
    if (!write_tag(out_buf, out_cap, &pos, 1, 0)) return -1;
    if (!write_varint(out_buf, out_cap, &pos, frame->seq_id)) return -1;

    /* Field 2: log_id */
    if (!write_tag(out_buf, out_cap, &pos, 2, 0)) return -1;
    if (!write_varint(out_buf, out_cap, &pos, frame->log_id)) return -1;

    /* Field 3: service */
    if (!write_tag(out_buf, out_cap, &pos, 3, 0)) return -1;
    if (!write_varint(out_buf, out_cap, &pos, (uint64_t)(uint32_t)frame->service)) return -1;

    /* Field 4: method */
    if (!write_tag(out_buf, out_cap, &pos, 4, 0)) return -1;
    if (!write_varint(out_buf, out_cap, &pos, (uint64_t)(uint32_t)frame->method)) return -1;

    /* Field 5: headers (repeated) */
    for (size_t i = 0; i < frame->header_count; i++) {
        uint8_t hdr_buf[256];
        size_t hdr_len = 0;
        if (!encode_header(hdr_buf, sizeof(hdr_buf), &hdr_len,
                           frame->headers[i].key, frame->headers[i].value))
            return -1;
        if (!write_bytes(out_buf, out_cap, &pos, 5, hdr_buf, hdr_len))
            return -1;
    }

    /* Field 8: payload */
    if (payload && payload_len > 0) {
        if (!write_bytes(out_buf, out_cap, &pos, 8, payload, payload_len))
            return -1;
    }

    return (int)pos;
}

int feishu_proto_build_ping(int32_t service_id, uint8_t *out_buf, size_t out_cap)
{
    feishu_frame_t ping = {0};
    ping.service = service_id;
    ping.method = 0;
    ping.header_count = 1;
    strncpy(ping.headers[0].key, "type", sizeof(ping.headers[0].key) - 1);
    strncpy(ping.headers[0].value, "ping", sizeof(ping.headers[0].value) - 1);

    return feishu_proto_build_frame(&ping, NULL, 0, out_buf, out_cap);
}

#pragma once
/*
 * T113Claw Audio IPC Protocol
 *
 * Shared header between t113claw (main process) and t113claw_audio (audio process).
 * Communication via Unix domain socket with framed messages.
 *
 * Architecture:
 *   t113claw_audio:  ALSA capture/playback + codec init + GPIO amp control
 *   t113claw:        IPC client, sends play commands, receives captured audio
 */

#include <stdint.h>

/* ── Socket path ──────────────────────────────────────────── */
#define AUDIO_IPC_SOCK_PATH     "/tmp/t113claw_audio.sock"

/* ── Message frame ────────────────────────────────────────── */
/*
 * Wire format:
 *   [magic:4][type:2][flags:2][length:4][payload:length]
 *
 * All fields are little-endian.
 */

#define AUDIO_IPC_MAGIC         0x4D434157  /* "MCAW" */

typedef struct __attribute__((packed)) {
    uint32_t magic;         /* AUDIO_IPC_MAGIC */
    uint16_t type;          /* audio_ipc_type_t */
    uint16_t flags;         /* reserved, set to 0 */
    uint32_t length;        /* payload length in bytes */
} audio_ipc_hdr_t;

#define AUDIO_IPC_HDR_SIZE      sizeof(audio_ipc_hdr_t)  /* 12 bytes */
#define AUDIO_IPC_MAX_PAYLOAD   (32 * 1024)  /* 32KB max payload */

/* ── Message types ────────────────────────────────────────── */
typedef enum {
    /* Commands: t113claw → t113claw_audio */
    AUDIO_CMD_RECORD_START  = 0x0001,   /* Start capturing audio */
    AUDIO_CMD_RECORD_STOP   = 0x0002,   /* Stop capturing audio */
    AUDIO_CMD_PLAY_START    = 0x0003,   /* Prepare for playback */
    AUDIO_CMD_PLAY_DATA     = 0x0004,   /* PCM data chunk for playback */
    AUDIO_CMD_PLAY_STOP     = 0x0005,   /* Graceful stop: drain ring buffer + ALSA, then disable amp */
    AUDIO_CMD_VOLUME_SET    = 0x0006,   /* Set playback volume (payload: uint8_t 0-100) */
    AUDIO_CMD_STATUS_GET    = 0x0007,   /* Request status */
    AUDIO_CMD_PLAY_FLUSH    = 0x0008,   /* Immediate stop: drop buffered data, disable amp */
    AUDIO_CMD_SHUTDOWN      = 0x00FF,   /* Graceful shutdown */

    /* Events: t113claw_audio → t113claw */
    AUDIO_EVT_AUDIO_DATA   = 0x0101,   /* Captured PCM data chunk */
    AUDIO_EVT_STATUS       = 0x0102,   /* Status response */
    AUDIO_EVT_ERROR        = 0x0103,   /* Error notification */
    AUDIO_EVT_ACK          = 0x0104,   /* Command acknowledgment */
    AUDIO_EVT_PLAY_DONE    = 0x0105,   /* Play stop/flush completed (drain finished) */
} audio_ipc_type_t;

/* ── Status payload ───────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  recording;     /* 1 = capturing, 0 = idle */
    uint8_t  playing;       /* 1 = playing, 0 = idle */
    uint8_t  volume;        /* 0-100 */
    uint8_t  amp_on;        /* 1 = amplifier enabled */
    uint32_t sample_rate;   /* capture sample rate */
    uint16_t channels;      /* capture channels */
    uint16_t format;        /* ALSA format (SND_PCM_FORMAT_S16_LE = 2) */
} audio_ipc_status_t;

/* ── Audio parameters ─────────────────────────────────────── */
#define AUDIO_SAMPLE_RATE       16000
#define AUDIO_CAPTURE_CHANNELS  1       /* mono mic input */
#define AUDIO_PLAY_CHANNELS     2       /* stereo output (T113 codec) */
#define AUDIO_FORMAT_BITS       16      /* S16_LE */
#define AUDIO_FRAME_DURATION_MS 30      /* 30ms frames for IPC */

/* Derived constants */
#define AUDIO_CAPTURE_FRAME_BYTES   (AUDIO_SAMPLE_RATE * AUDIO_CAPTURE_CHANNELS * (AUDIO_FORMAT_BITS / 8) * AUDIO_FRAME_DURATION_MS / 1000)
#define AUDIO_PLAY_FRAME_BYTES      (AUDIO_SAMPLE_RATE * AUDIO_PLAY_CHANNELS * (AUDIO_FORMAT_BITS / 8) * AUDIO_FRAME_DURATION_MS / 1000)

/* ALSA device */
#define AUDIO_PCM_DEVICE        "hw:0,0"

/* GPIO for amplifier (PB2 = GPIO34) */
#define AUDIO_AMP_GPIO          34
#define AUDIO_AMP_GPIO_PATH     "/sys/class/gpio/gpio34/value"

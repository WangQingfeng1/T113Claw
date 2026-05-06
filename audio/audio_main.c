/*
 * T113Claw Audio Process — Main Entry Point
 *
 * Standalone process for audio capture and playback on T113-S3.
 * Communicates with the T113Claw main process via Unix domain socket.
 *
 * Architecture:
 *   ┌───────────────────────────────────────────┐
 *   │             t113claw_audio                   │
 *   │                                            │
 *   │  ┌──────────┐  ┌───────────┐  ┌────────┐ │
 *   │  │ Capture   │  │ Playback  │  │ Codec  │ │
 *   │  │ Thread    │  │ Thread    │  │ Init   │ │
 *   │  └─────┬─────┘  └─────┬─────┘  └────────┘ │
 *   │        │              │                     │
 *   │  ┌─────┴──────────────┴─────┐              │
 *   │  │     IPC Server           │              │
 *   │  │  (Unix Domain Socket)    │              │
 *   │  └────────────┬─────────────┘              │
 *   └───────────────┼───────────────────────────┘
 *                   │
 *           /tmp/t113claw_audio.sock
 *                   │
 *   ┌───────────────┼────────────────────────────┐
 *   │  t113claw       │                             │
 *   │  (audio_service.c — IPC client)             │
 *   └────────────────────────────────────────────┘
 */

#include "audio_ipc.h"
#include "audio_codec.h"
#include "audio_capture.h"
#include "audio_playback.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>

#define TAG "audio_main"
#define LOG_I(tag, fmt, ...) fprintf(stderr, "[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_E(tag, fmt, ...) fprintf(stderr, "[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...) fprintf(stderr, "[W][%s] " fmt "\n", tag, ##__VA_ARGS__)

static volatile int s_running = 1;
static int s_client_fd = -1;
static pthread_mutex_t s_send_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Signal handler ───────────────────────────────────────── */

static void signal_handler(int sig)
{
    (void)sig;
    s_running = 0;
}

/* ── Reliable socket I/O ──────────────────────────────────── */

static int send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t n = write(fd, p, remaining);
        if (n <= 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += n;
        remaining -= n;
    }
    return 0;
}

static int recv_all(int fd, void *buf, size_t len)
{
    uint8_t *p = buf;
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t n = read(fd, p, remaining);
        if (n == 0) return -1;  /* connection closed */
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += n;
        remaining -= n;
    }
    return 0;
}

/* ── Send IPC message (thread-safe) ───────────────────────── */

static int ipc_send_msg(int fd, uint16_t type, const void *payload, uint32_t length)
{
    if (fd < 0) return -1;

    audio_ipc_hdr_t hdr = {
        .magic  = AUDIO_IPC_MAGIC,
        .type   = type,
        .flags  = 0,
        .length = length,
    };

    pthread_mutex_lock(&s_send_mutex);
    int rc = send_all(fd, &hdr, AUDIO_IPC_HDR_SIZE);
    if (rc == 0 && length > 0 && payload) {
        rc = send_all(fd, payload, length);
    }
    pthread_mutex_unlock(&s_send_mutex);
    return rc;
}

/* ── Playback ring buffer (IPC → playback thread) ─────────── */

#define PLAY_RING_SIZE  (1024 * 1024) /* 1MB ring buffer (~16s stereo 16kHz) */

static uint8_t  s_play_ring[PLAY_RING_SIZE];
static size_t   s_play_rd = 0;
static size_t   s_play_wr = 0;
static pthread_mutex_t s_play_mutex   = PTHREAD_MUTEX_INITIALIZER;
static volatile int    s_play_flushing = 0; /* set to 1 on PLAY_STOP to unblock writer */

static size_t play_ring_avail(void)
{
    return (s_play_wr - s_play_rd + PLAY_RING_SIZE) % PLAY_RING_SIZE;
}

static size_t play_ring_free(void)
{
    return PLAY_RING_SIZE - 1 - play_ring_avail();
}

/*
 * Blocking write: waits for space instead of dropping data.
 * Unblocked by s_play_flushing flag (set by PLAY_FLUSH).
 */
static void play_ring_write(const uint8_t *data, size_t len)
{
    size_t offset = 0;
    pthread_mutex_lock(&s_play_mutex);
    while (offset < len && !s_play_flushing) {
        size_t avail = play_ring_free();
        if (avail == 0) {
            /* Wait for playback thread to consume data */
            pthread_mutex_unlock(&s_play_mutex);
            usleep(5000); /* 5ms */
            pthread_mutex_lock(&s_play_mutex);
            continue;
        }
        size_t chunk = len - offset;
        if (chunk > avail) chunk = avail;
        for (size_t i = 0; i < chunk; i++) {
            s_play_ring[s_play_wr] = data[offset + i];
            s_play_wr = (s_play_wr + 1) % PLAY_RING_SIZE;
        }
        offset += chunk;
    }
    pthread_mutex_unlock(&s_play_mutex);
}

static size_t play_ring_read(uint8_t *data, size_t max_len)
{
    pthread_mutex_lock(&s_play_mutex);
    size_t avail = play_ring_avail();
    size_t to_read = (avail < max_len) ? avail : max_len;
    for (size_t i = 0; i < to_read; i++) {
        data[i] = s_play_ring[s_play_rd];
        s_play_rd = (s_play_rd + 1) % PLAY_RING_SIZE;
    }
    pthread_mutex_unlock(&s_play_mutex);
    return to_read;
}

static void play_ring_clear(void)
{
    pthread_mutex_lock(&s_play_mutex);
    s_play_rd = s_play_wr = 0;
    s_play_flushing = 0;
    pthread_mutex_unlock(&s_play_mutex);
}

/* ── Callbacks ────────────────────────────────────────────── */

/* Called from capture thread: send recorded data to T113Claw */
static void on_capture_data(const uint8_t *buffer, size_t size, void *user)
{
    (void)user;
    ipc_send_msg(s_client_fd, AUDIO_EVT_AUDIO_DATA, buffer, (uint32_t)size);
}

/* Called from playback thread: pull data from ring buffer */
static int on_play_request(uint8_t *buffer, size_t size, void *user)
{
    (void)user;
    return (int)play_ring_read(buffer, size);
}

/* ── Process a single IPC command ─────────────────────────── */

static void handle_command(int fd, const audio_ipc_hdr_t *hdr, const uint8_t *payload)
{
    switch (hdr->type) {
    case AUDIO_CMD_RECORD_START:
        LOG_I(TAG, "CMD: Record start");
        if (!audio_capture_is_active()) {
            audio_capture_start(on_capture_data, NULL);
        }
        ipc_send_msg(fd, AUDIO_EVT_ACK, NULL, 0);
        break;

    case AUDIO_CMD_RECORD_STOP:
        LOG_I(TAG, "CMD: Record stop");
        audio_capture_stop();
        ipc_send_msg(fd, AUDIO_EVT_ACK, NULL, 0);
        break;

    case AUDIO_CMD_PLAY_START:
        LOG_I(TAG, "CMD: Play start");
        play_ring_clear();
        if (!audio_playback_is_active()) {
            audio_playback_start(on_play_request, NULL);
        }
        ipc_send_msg(fd, AUDIO_EVT_ACK, NULL, 0);
        break;

    case AUDIO_CMD_PLAY_DATA:
        /* Write PCM data into the playback ring buffer */
        if (hdr->length > 0 && payload) {
            play_ring_write(payload, hdr->length);
        }
        /* No ACK for data messages (too frequent) */
        break;

    case AUDIO_CMD_PLAY_STOP:
        LOG_I(TAG, "CMD: Play stop (drain)");
        /* Graceful stop: playback thread drains ring buffer + ALSA, then exits */
        audio_playback_drain();
        play_ring_clear();
        ipc_send_msg(fd, AUDIO_EVT_PLAY_DONE, NULL, 0);
        break;

    case AUDIO_CMD_PLAY_FLUSH:
        LOG_I(TAG, "CMD: Play flush (immediate stop)");
        s_play_flushing = 1; /* unblock any blocked ring buffer writer */
        audio_playback_stop();
        play_ring_clear();
        ipc_send_msg(fd, AUDIO_EVT_PLAY_DONE, NULL, 0);
        break;

    case AUDIO_CMD_VOLUME_SET:
        if (hdr->length >= 1 && payload) {
            int vol = payload[0];
            LOG_I(TAG, "CMD: Volume set to %d%%", vol);
            audio_codec_set_volume(vol);
        }
        ipc_send_msg(fd, AUDIO_EVT_ACK, NULL, 0);
        break;

    case AUDIO_CMD_STATUS_GET: {
        audio_ipc_status_t status = {
            .recording   = (uint8_t)audio_capture_is_active(),
            .playing     = (uint8_t)audio_playback_is_active(),
            .volume      = (uint8_t)audio_codec_get_volume(),
            .amp_on      = (uint8_t)audio_playback_is_active(),
            .sample_rate = AUDIO_SAMPLE_RATE,
            .channels    = AUDIO_CAPTURE_CHANNELS,
            .format      = 2,  /* SND_PCM_FORMAT_S16_LE */
        };
        ipc_send_msg(fd, AUDIO_EVT_STATUS, &status, sizeof(status));
        break;
    }

    case AUDIO_CMD_SHUTDOWN:
        LOG_I(TAG, "CMD: Shutdown requested");
        s_running = 0;
        ipc_send_msg(fd, AUDIO_EVT_ACK, NULL, 0);
        break;

    default:
        LOG_W(TAG, "Unknown command type: 0x%04x", hdr->type);
        break;
    }
}

/* ── IPC Server ───────────────────────────────────────────── */

static int create_server_socket(void)
{
    /* Remove stale socket file */
    unlink(AUDIO_IPC_SOCK_PATH);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_E(TAG, "socket() failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, AUDIO_IPC_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_E(TAG, "bind() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, 1) < 0) {
        LOG_E(TAG, "listen() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    LOG_I(TAG, "IPC server listening on %s", AUDIO_IPC_SOCK_PATH);
    return fd;
}

static void serve_client(int client_fd)
{
    s_client_fd = client_fd;
    LOG_I(TAG, "Client connected");

    uint8_t *payload_buf = malloc(AUDIO_IPC_MAX_PAYLOAD);
    if (!payload_buf) {
        LOG_E(TAG, "Failed to allocate payload buffer");
        return;
    }

    while (s_running) {
        /* Read header */
        audio_ipc_hdr_t hdr;
        if (recv_all(client_fd, &hdr, AUDIO_IPC_HDR_SIZE) != 0) {
            LOG_I(TAG, "Client disconnected");
            break;
        }

        /* Validate */
        if (hdr.magic != AUDIO_IPC_MAGIC) {
            LOG_E(TAG, "Invalid magic: 0x%08x", hdr.magic);
            break;
        }
        if (hdr.length > AUDIO_IPC_MAX_PAYLOAD) {
            LOG_E(TAG, "Payload too large: %u", hdr.length);
            break;
        }

        /* Read payload */
        if (hdr.length > 0) {
            if (recv_all(client_fd, payload_buf, hdr.length) != 0) {
                LOG_E(TAG, "Failed to read payload");
                break;
            }
        }

        handle_command(client_fd, &hdr, payload_buf);
    }

    free(payload_buf);
    s_client_fd = -1;

    /* Stop capture/playback when client disconnects */
    audio_capture_stop();
    audio_playback_stop();
}

/* ── Main ─────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("┌─────────────────────────────────────┐\n"
           "│  T113Claw Audio Process v0.1.0        │\n"
           "│  T113-S3 ALSA Audio Server          │\n"
           "└─────────────────────────────────────┘\n\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);  /* Ignore broken pipe */

    /* Initialize subsystems */
    audio_codec_init();
    audio_capture_init();
    audio_playback_init();

    /* Create IPC server */
    int server_fd = create_server_socket();
    if (server_fd < 0) {
        LOG_E(TAG, "Failed to create IPC server");
        return 1;
    }

    /* Accept connections in a loop */
    while (s_running) {
        LOG_I(TAG, "Waiting for T113Claw connection...");

        /* Set timeout on accept via select */
        fd_set rfds;
        struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
        FD_ZERO(&rfds);
        FD_SET(server_fd, &rfds);

        int ret = select(server_fd + 1, &rfds, NULL, NULL, &tv);
        if (ret <= 0) continue;  /* timeout or error, retry */

        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            LOG_E(TAG, "accept() failed: %s", strerror(errno));
            continue;
        }

        serve_client(client_fd);
        close(client_fd);
    }

    /* Cleanup */
    audio_capture_stop();
    audio_playback_stop();
    audio_amp_enable(0);

    close(server_fd);
    unlink(AUDIO_IPC_SOCK_PATH);

    LOG_I(TAG, "Audio process stopped. Goodbye.");
    return 0;
}

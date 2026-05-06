/*
 * T113Claw Audio Service — IPC Client
 *
 * Manages the t113claw_audio child process and communicates via Unix domain
 * socket.  On T113, forks the real audio process; on x86 simulator, runs
 * as a no-op stub.
 *
 * IPC protocol: see audio/audio_ipc.h
 */

#include "audio_service.h"
#include "config/config.h"
#include "utils/log.h"
#include "utils/utils.h"
#include "t113claw_config.h"

/* Shared IPC header — used by both processes */
#include "audio_ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#define TAG "audio"

/* ── State ────────────────────────────────────────────────── */

static pid_t       s_audio_pid = -1;
static int         s_sock_fd = -1;
static pthread_t   s_recv_thread;
static volatile int s_connected = 0;
static volatile int s_recv_running = 0;

static audio_data_cb_t s_data_cb = NULL;
static void           *s_data_cb_user = NULL;
static pthread_mutex_t s_send_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Synchronous ACK waiting ──────────────────────────────── */
static pthread_mutex_t s_ack_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_ack_cond  = PTHREAD_COND_INITIALIZER;
static volatile int    s_ack_flag  = 0;

/* Path to the audio process binary (next to t113claw) */
static char s_audio_bin[512] = "";

/* ── Socket I/O helpers ───────────────────────────────────── */

static int send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    size_t rem = len;
    while (rem > 0) {
        ssize_t n = write(fd, p, rem);
        if (n <= 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += n;
        rem -= n;
    }
    return 0;
}

static int recv_all(int fd, void *buf, size_t len)
{
    uint8_t *p = buf;
    size_t rem = len;
    while (rem > 0) {
        ssize_t n = read(fd, p, rem);
        if (n == 0) return -1;
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += n;
        rem -= n;
    }
    return 0;
}

/* ── Send IPC message ─────────────────────────────────────── */

static int ipc_send(uint16_t type, const void *payload, uint32_t length)
{
    if (s_sock_fd < 0 || !s_connected) return MC_ERR;

    audio_ipc_hdr_t hdr = {
        .magic  = AUDIO_IPC_MAGIC,
        .type   = type,
        .flags  = 0,
        .length = length,
    };

    pthread_mutex_lock(&s_send_mutex);
    int rc = send_all(s_sock_fd, &hdr, AUDIO_IPC_HDR_SIZE);
    if (rc == 0 && length > 0 && payload) {
        rc = send_all(s_sock_fd, payload, length);
    }
    pthread_mutex_unlock(&s_send_mutex);

    if (rc != 0) {
        LOG_E(TAG, "IPC send failed");
        s_connected = 0;
    }
    return rc == 0 ? MC_OK : MC_ERR;
}

/* ── Receive thread (handles events from audio process) ───── */

static void *recv_thread_func(void *arg)
{
    (void)arg;
    uint8_t *payload = malloc(AUDIO_IPC_MAX_PAYLOAD);
    if (!payload) {
        LOG_E(TAG, "Failed to allocate recv buffer");
        return NULL;
    }

    while (s_recv_running && s_connected) {
        audio_ipc_hdr_t hdr;
        if (recv_all(s_sock_fd, &hdr, AUDIO_IPC_HDR_SIZE) != 0) {
            LOG_W(TAG, "Audio process disconnected");
            s_connected = 0;
            break;
        }

        if (hdr.magic != AUDIO_IPC_MAGIC) {
            LOG_E(TAG, "Invalid IPC magic: 0x%08x", hdr.magic);
            s_connected = 0;
            break;
        }
        if (hdr.length > AUDIO_IPC_MAX_PAYLOAD) {
            LOG_E(TAG, "IPC payload too large: %u", hdr.length);
            s_connected = 0;
            break;
        }

        if (hdr.length > 0) {
            if (recv_all(s_sock_fd, payload, hdr.length) != 0) {
                s_connected = 0;
                break;
            }
        }

        switch (hdr.type) {
        case AUDIO_EVT_AUDIO_DATA:
            if (s_data_cb && hdr.length > 0) {
                s_data_cb(payload, hdr.length, s_data_cb_user);
            }
            break;

        case AUDIO_EVT_STATUS:
            if (hdr.length >= sizeof(audio_ipc_status_t)) {
                audio_ipc_status_t *st = (audio_ipc_status_t *)payload;
                LOG_D(TAG, "Status: rec=%d play=%d vol=%d amp=%d",
                       st->recording, st->playing, st->volume, st->amp_on);
            }
            break;

        case AUDIO_EVT_ACK:
            /* Command acknowledged */
            break;

        case AUDIO_EVT_PLAY_DONE:
            /* Play stop/flush completed — wake anyone blocking on it */
            pthread_mutex_lock(&s_ack_mutex);
            s_ack_flag = 1;
            pthread_cond_signal(&s_ack_cond);
            pthread_mutex_unlock(&s_ack_mutex);
            break;

        case AUDIO_EVT_ERROR:
            LOG_W(TAG, "Audio process error (len=%u)", hdr.length);
            break;

        default:
            LOG_W(TAG, "Unknown event type: 0x%04x", hdr.type);
            break;
        }
    }

    free(payload);
    return NULL;
}

/* ── Connect to audio process IPC ─────────────────────────── */

static int connect_ipc(void)
{
    s_sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s_sock_fd < 0) {
        LOG_E(TAG, "socket() failed: %s", strerror(errno));
        return MC_ERR;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, AUDIO_IPC_SOCK_PATH, sizeof(addr.sun_path) - 1);

    /* Retry connection (audio process may need time to start) */
    int attempts = 0;
    while (attempts < 30) {
        if (connect(s_sock_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            s_connected = 1;
            LOG_I(TAG, "Connected to audio process");

            /* Start receive thread */
            s_recv_running = 1;
            pthread_create(&s_recv_thread, NULL, recv_thread_func, NULL);
            return MC_OK;
        }
        attempts++;
        usleep(200000);  /* 200ms retry interval */
    }

    LOG_E(TAG, "Failed to connect to audio process after %d attempts", attempts);
    close(s_sock_fd);
    s_sock_fd = -1;
    return MC_ERR;
}

/* ── Resolve audio binary path ────────────────────────────── */

static void find_audio_binary(void)
{
    /* Try same directory as t113claw (for T113 deployment) */
    char self_path[256];
    ssize_t len = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (len > 0) {
        self_path[len] = '\0';
        /* Replace "t113claw" with "t113claw_audio" */
        char *last_slash = strrchr(self_path, '/');
        if (last_slash && (size_t)(last_slash - self_path + 1) < sizeof(s_audio_bin) - 16) {
            *(last_slash + 1) = '\0';
            snprintf(s_audio_bin, sizeof(s_audio_bin), "%st113claw_audio", self_path);
            if (access(s_audio_bin, X_OK) == 0) {
                LOG_I(TAG, "Audio binary: %s", s_audio_bin);
                return;
            }
        }
    }

    /* Fallback: in build directory */
    snprintf(s_audio_bin, sizeof(s_audio_bin), "./t113claw_audio");
    if (access(s_audio_bin, X_OK) == 0) {
        LOG_I(TAG, "Audio binary: %s", s_audio_bin);
        return;
    }

    /* Not found */
    s_audio_bin[0] = '\0';
    LOG_W(TAG, "Audio binary not found, audio disabled");
}

/* ── Public API ───────────────────────────────────────────── */

int audio_service_init(void)
{
#ifdef SIMULATOR_LINUX
    LOG_I(TAG, "Audio service initialized (simulator stub)");
    return MC_OK;
#endif
    find_audio_binary();
    LOG_I(TAG, "Audio service initialized");
    return MC_OK;
}

int audio_service_start(void)
{
#ifdef SIMULATOR_LINUX
    LOG_I(TAG, "Audio service started (simulator no-op)");
    return MC_OK;
#endif

    if (s_audio_bin[0] == '\0') {
        LOG_W(TAG, "Audio binary not found, skipping");
        return MC_OK;
    }

    /* Fork the audio process */
    s_audio_pid = fork();
    if (s_audio_pid < 0) {
        LOG_E(TAG, "fork() failed: %s", strerror(errno));
        return MC_ERR;
    }

    if (s_audio_pid == 0) {
        /* Child: exec the audio process */
        execl(s_audio_bin, "t113claw_audio", (char *)NULL);
        /* execl only returns on error */
        fprintf(stderr, "[E][audio] execl(%s) failed: %s\n", s_audio_bin, strerror(errno));
        _exit(1);
    }

    LOG_I(TAG, "Audio process started (pid=%d)", s_audio_pid);

    /* Parent: connect to the audio process IPC */
    usleep(500000);  /* Give audio process 500ms to start */
    int rc = connect_ipc();
    if (rc != MC_OK) {
        LOG_E(TAG, "Failed to connect to audio process");
        return rc;
    }

    {
        int volume_pct = atoi(config_get_default("system", "volume", "70"));
        if (volume_pct < 60) volume_pct = 60;
        if (volume_pct > 80) volume_pct = 80;
        if (audio_service_set_volume(volume_pct) != MC_OK) {
            LOG_W(TAG, "Failed to apply saved volume: %d", volume_pct);
        }
    }

    return rc;
}

void audio_service_stop(void)
{
#ifdef SIMULATOR_LINUX
    LOG_I(TAG, "Audio service stopped (simulator)");
    return;
#endif

    /* Send shutdown command */
    if (s_connected) {
        ipc_send(AUDIO_CMD_SHUTDOWN, NULL, 0);
        usleep(200000);  /* 200ms grace */
    }

    /* Stop receive thread */
    s_recv_running = 0;
    if (s_connected) {
        shutdown(s_sock_fd, SHUT_RDWR);
    }
    if (s_recv_thread) {
        pthread_join(s_recv_thread, NULL);
        s_recv_thread = 0;
    }

    /* Close socket */
    if (s_sock_fd >= 0) {
        close(s_sock_fd);
        s_sock_fd = -1;
    }
    s_connected = 0;

    /* Kill child process */
    if (s_audio_pid > 0) {
        kill(s_audio_pid, SIGTERM);
        int status;
        waitpid(s_audio_pid, &status, WNOHANG);
        usleep(500000);
        /* Force kill if still alive */
        if (waitpid(s_audio_pid, &status, WNOHANG) == 0) {
            kill(s_audio_pid, SIGKILL);
            waitpid(s_audio_pid, &status, 0);
        }
        LOG_I(TAG, "Audio process stopped (pid=%d)", s_audio_pid);
        s_audio_pid = -1;
    }

    LOG_I(TAG, "Audio service stopped");
}

int audio_service_record_start(audio_data_cb_t cb, void *user)
{
    s_data_cb = cb;
    s_data_cb_user = user;
    return ipc_send(AUDIO_CMD_RECORD_START, NULL, 0);
}

int audio_service_record_stop(void)
{
    int rc = ipc_send(AUDIO_CMD_RECORD_STOP, NULL, 0);
    s_data_cb = NULL;
    s_data_cb_user = NULL;
    return rc;
}

int audio_service_play_start(void)
{
    return ipc_send(AUDIO_CMD_PLAY_START, NULL, 0);
}

int audio_service_play_pcm(const uint8_t *pcm, size_t len)
{
    /* Send in chunks to respect max payload */
    size_t offset = 0;
    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > AUDIO_IPC_MAX_PAYLOAD)
            chunk = AUDIO_IPC_MAX_PAYLOAD;
        int rc = ipc_send(AUDIO_CMD_PLAY_DATA, pcm + offset, (uint32_t)chunk);
        if (rc != MC_OK) return rc;
        offset += chunk;
    }
    return MC_OK;
}

/* Wait for ACK from audio process with timeout (seconds). Returns 0 on ACK, -1 on timeout. */
static int wait_for_ack(int timeout_sec)
{
    pthread_mutex_lock(&s_ack_mutex);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_sec;
    while (!s_ack_flag && s_connected) {
        int r = pthread_cond_timedwait(&s_ack_cond, &s_ack_mutex, &ts);
        if (r == ETIMEDOUT) {
            pthread_mutex_unlock(&s_ack_mutex);
            return -1;
        }
    }
    pthread_mutex_unlock(&s_ack_mutex);
    return 0;
}

int audio_service_play_stop(void)
{
    pthread_mutex_lock(&s_ack_mutex);
    s_ack_flag = 0;
    pthread_mutex_unlock(&s_ack_mutex);

    int rc = ipc_send(AUDIO_CMD_PLAY_STOP, NULL, 0);
    if (rc != MC_OK) return rc;

    /* Block until audio process finishes drain + sends ACK */
    if (wait_for_ack(30) < 0)
        LOG_W(TAG, "play_stop ACK timeout (30s)");
    return MC_OK;
}

int audio_service_play_flush(void)
{
    pthread_mutex_lock(&s_ack_mutex);
    s_ack_flag = 0;
    pthread_mutex_unlock(&s_ack_mutex);

    int rc = ipc_send(AUDIO_CMD_PLAY_FLUSH, NULL, 0);
    if (rc != MC_OK) return rc;

    /* Block until audio process stops + sends ACK */
    if (wait_for_ack(5) < 0)
        LOG_W(TAG, "play_flush ACK timeout (5s)");
    return MC_OK;
}

int audio_service_set_volume(int volume_pct)
{
    uint8_t vol = (uint8_t)(volume_pct < 0 ? 0 : (volume_pct > 100 ? 100 : volume_pct));
    return ipc_send(AUDIO_CMD_VOLUME_SET, &vol, 1);
}

int audio_service_is_running(void)
{
    return s_connected && s_audio_pid > 0;
}

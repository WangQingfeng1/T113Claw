#include "message_bus.h"
#include "t113claw_config.h"
#include "utils/log.h"
#include "utils/utils.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#define TAG "bus"

/* ── Thread-safe ring buffer queue ───────────────────────── */

typedef struct {
    mc_msg_t     items[T113CLAW_BUS_QUEUE_LEN];
    int          head;
    int          tail;
    int          count;
    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} msg_queue_t;

static msg_queue_t s_inbound;
static msg_queue_t s_outbound;

static void queue_init(msg_queue_t *q)
{
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

static void queue_destroy(msg_queue_t *q)
{
    pthread_mutex_lock(&q->mutex);
    /* Free any remaining messages */
    while (q->count > 0) {
        free(q->items[q->head].content);
        q->head = (q->head + 1) % T113CLAW_BUS_QUEUE_LEN;
        q->count--;
    }
    pthread_mutex_unlock(&q->mutex);

    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
}

static int queue_push(msg_queue_t *q, const mc_msg_t *msg)
{
    pthread_mutex_lock(&q->mutex);

    /* Wait up to 1 second for space */
    if (q->count >= T113CLAW_BUS_QUEUE_LEN) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        while (q->count >= T113CLAW_BUS_QUEUE_LEN) {
            if (pthread_cond_timedwait(&q->not_full, &q->mutex, &ts) != 0) {
                pthread_mutex_unlock(&q->mutex);
                LOG_W(TAG, "Queue full, dropping message");
                return MC_ERR_NOMEM;
            }
        }
    }

    q->items[q->tail] = *msg;
    q->tail = (q->tail + 1) % T113CLAW_BUS_QUEUE_LEN;
    q->count++;

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
    return MC_OK;
}

static int queue_pop(msg_queue_t *q, mc_msg_t *msg, uint32_t timeout_ms)
{
    pthread_mutex_lock(&q->mutex);

    while (q->count == 0) {
        if (timeout_ms == 0) {
            /* Wait forever */
            pthread_cond_wait(&q->not_empty, &q->mutex);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec  += timeout_ms / 1000;
            ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000L;
            }

            if (pthread_cond_timedwait(&q->not_empty, &q->mutex, &ts) != 0) {
                pthread_mutex_unlock(&q->mutex);
                return MC_ERR_TIMEOUT;
            }
        }
    }

    *msg = q->items[q->head];
    q->head = (q->head + 1) % T113CLAW_BUS_QUEUE_LEN;
    q->count--;

    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    return MC_OK;
}

/* ── Public API ───────────────────────────────────────────── */

int message_bus_init(void)
{
    queue_init(&s_inbound);
    queue_init(&s_outbound);
    LOG_I(TAG, "Message bus initialized (queue depth %d)", T113CLAW_BUS_QUEUE_LEN);
    return MC_OK;
}

void message_bus_destroy(void)
{
    queue_destroy(&s_inbound);
    queue_destroy(&s_outbound);
    LOG_I(TAG, "Message bus destroyed");
}

int message_bus_push_inbound(const mc_msg_t *msg)
{
    return queue_push(&s_inbound, msg);
}

int message_bus_pop_inbound(mc_msg_t *msg, uint32_t timeout_ms)
{
    return queue_pop(&s_inbound, msg, timeout_ms);
}

int message_bus_push_outbound(const mc_msg_t *msg)
{
    return queue_push(&s_outbound, msg);
}

int message_bus_pop_outbound(mc_msg_t *msg, uint32_t timeout_ms)
{
    return queue_pop(&s_outbound, msg, timeout_ms);
}

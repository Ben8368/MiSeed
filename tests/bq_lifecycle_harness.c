// SPDX-License-Identifier: AGPL-3.0-only
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
typedef int irqreturn_t;
#define IRQ_HANDLED 1
#define dev_err_ratelimited(...) ((void)0)
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
struct bq25970_device {
    pthread_mutex_t event_lock;
    void (*event)(void *);
    void *event_data;
    int *map;
    struct { struct { void *parent; } dev; } aux;
};
static int stops, fail_stop;
static int bq25970_disable(int *map) { stops++; return fail_stop ? -EIO : 0; }
/* CHIP_UNDER_TEST */
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static bool entered, release_callback, detached;
static int callbacks;
static void callback(void *data)
{
    assert(data == &callbacks);
    pthread_mutex_lock(&gate);
    callbacks++; entered = true;
    pthread_cond_broadcast(&condition);
    while (!release_callback) pthread_cond_wait(&condition, &gate);
    pthread_mutex_unlock(&gate);
}
static void *irq_thread(void *data) { bq25970_irq(0, data); return NULL; }
static void *detach_thread(void *data)
{
    bq25970_unsubscribe(data);
    pthread_mutex_lock(&gate);
    detached = true;
    pthread_mutex_unlock(&gate);
    return NULL;
}
int main(void)
{
    struct bq25970_device bq = {.event_lock = PTHREAD_MUTEX_INITIALIZER};
    pthread_t irq, detach;
    assert(bq25970_subscribe(&bq, NULL, NULL) == -EINVAL);
    assert(bq25970_irq(0, &bq) == IRQ_HANDLED && stops == 1);
    assert(bq25970_subscribe(&bq, callback, &callbacks) == 0);
    assert(bq25970_subscribe(&bq, callback, NULL) == -EBUSY);
    assert(!pthread_create(&irq, NULL, irq_thread, &bq));
    pthread_mutex_lock(&gate);
    while (!entered) pthread_cond_wait(&condition, &gate);
    pthread_mutex_unlock(&gate);
    assert(!pthread_create(&detach, NULL, detach_thread, &bq));
    pthread_mutex_lock(&gate);
    assert(!detached);
    release_callback = true;
    pthread_cond_broadcast(&condition);
    pthread_mutex_unlock(&gate);
    pthread_join(irq, NULL); pthread_join(detach, NULL);
    assert(detached && callbacks == 1 && !bq.event && !bq.event_data);
    fail_stop = 1;
    assert(bq25970_irq(0, &bq) == IRQ_HANDLED && stops == 2 && callbacks == 1);
    bq25970_unsubscribe(&bq);
    return 0;
}

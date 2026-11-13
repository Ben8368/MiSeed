// SPDX-License-Identifier: AGPL-3.0-only
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
typedef int irqreturn_t;
#define IRQ_HANDLED 1
#define dev_err_ratelimited(...) ((void)0)
#define dev_info(...) ((void)0)
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
struct bq25970_device {
    pthread_mutex_t event_lock;
    void (*event)(void *);
    void *event_data;
    int *map;
    struct { struct { void *parent; } dev; } aux;
};
static int stops, fail_stop, pump_on;
static int bq25970_disable(int *map) {
    stops++;
    if (fail_stop) return -EIO;
    pump_on = 0;
    return 0;
}
enum umi_pps_state { PPS_OFF, PPS_FAULT };
enum { POWER_SUPPLY_PROP_ONLINE, POWER_SUPPLY_PROP_VOLTAGE_NOW,
       POWER_SUPPLY_PROP_CURRENT_NOW, POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT };
#define PPS_FIXED 1
#define UMI_CHARGE_USB_ICL_UA 3000000
#define UMI_PPS_PERIOD_MS 200
struct umi_pps {
    struct bq25970_device *chip;
    pthread_mutex_t lock;
    int work;
    void *pd, *sw;
    bool ready, removing, pps, claimed, configured, stop_pending;
    enum umi_pps_state state;
    int last_error, taper_start;
};
static int pd_online, fail_pd, fail_release, cancelled;
static int umi_pps_disable(struct umi_pps *c) { return bq25970_disable(c->chip->map); }
static int umi_pps_get(void *psy, int prop, int *value) {
    *value = prop == POWER_SUPPLY_PROP_ONLINE ? pd_online :
             prop == POWER_SUPPLY_PROP_VOLTAGE_NOW ? 5000000 : 3000000;
    return 0;
}
static int umi_pps_set(void *psy, int prop, int value) {
    if (prop == POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT) {
        assert(pd_online == PPS_FIXED && value == 5000000);
        return fail_pd ? -EIO : 0;
    }
    assert(prop == POWER_SUPPLY_PROP_ONLINE && value == PPS_FIXED);
    if (fail_pd) return -EIO;
    pd_online = value;
    return 0;
}
static int umi_smb5_release(void *psy, int fault) {
    assert(!pump_on && pd_online == PPS_FIXED);
    return fail_release ? -EIO : 0;
}
static void cancel_delayed_work_sync(int *work) { cancelled++; }
static void msleep(unsigned int ms);
/* CHIP_UNDER_TEST */
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static bool entered, release_callback, detached;
static int callbacks;
static int cleanup_sleeps, retry_budget;
static bool cleanup_done;
static void msleep(unsigned int ms)
{
    assert(ms == UMI_PPS_PERIOD_MS);
    pthread_mutex_lock(&gate);
    cleanup_sleeps++;
    pthread_cond_broadcast(&condition);
    while (!retry_budget) pthread_cond_wait(&condition, &gate);
    retry_budget--;
    pthread_mutex_unlock(&gate);
}
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
static void *cleanup_thread(void *data)
{
    umi_pps_cleanup(data);
    pthread_mutex_lock(&gate);
    cleanup_done = true;
    pthread_cond_broadcast(&condition);
    pthread_mutex_unlock(&gate);
    return NULL;
}
static void test_cleanup_retains_live_resources(struct bq25970_device *bq)
{
    for (int fault = 0; fault <= 3; fault++) {
        struct umi_pps c = {.chip = bq, .lock = PTHREAD_MUTEX_INITIALIZER,
                           .ready = true, .pps = true, .claimed = true};
        pthread_t thread;
        fail_stop = fault == 1;
        fail_pd = fault == 2;
        fail_release = fault == 3;
        pump_on = 1; pd_online = 2;
        cancelled = cleanup_sleeps = retry_budget = 0; cleanup_done = false;
        assert(!bq25970_subscribe(bq, callback, &callbacks));
        assert(!pthread_create(&thread, NULL, cleanup_thread, &c));
        for (int attempt = 1; fault && attempt <= 3; attempt++) {
            pthread_mutex_lock(&gate);
            while (cleanup_sleeps < attempt) pthread_cond_wait(&condition, &gate);
            assert(!cleanup_done && cancelled == 1);
            pthread_mutex_lock(&c.lock);
            assert(c.removing && !c.ready && c.claimed && c.stop_pending);
            pthread_mutex_unlock(&c.lock);
            pthread_mutex_lock(&bq->event_lock);
            assert(bq->event && bq->event_data == &callbacks);
            pthread_mutex_unlock(&bq->event_lock);
            if (attempt == 3) fail_stop = fail_pd = fail_release = 0;
            retry_budget++;
            pthread_cond_broadcast(&condition);
            pthread_mutex_unlock(&gate);
        }
        pthread_join(thread, NULL);
        assert(cleanup_done && !pump_on && pd_online == PPS_FIXED);
        assert(!c.claimed && !c.pps && !c.stop_pending && !bq->event);
        assert(c.removing && !c.ready && cancelled == 1);
        umi_pps_cleanup(&c); /* shutdown followed by devres cleanup is harmless */
        pthread_mutex_destroy(&c.lock);
    }
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
    test_cleanup_retains_live_resources(&bq);
    return 0;
}

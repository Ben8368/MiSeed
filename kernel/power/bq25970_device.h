/* SPDX-License-Identifier: GPL-2.0-only */
/* Kernel-only chip/policy attachment. No userspace ABI. */
#ifndef MISEED_BQ25970_DEVICE_H
#define MISEED_BQ25970_DEVICE_H
#include <linux/auxiliary_bus.h>
#include <linux/mutex.h>
struct regmap;
struct bq25970_device {
	struct auxiliary_device aux;
	struct regmap *map;
	/* Serializes callback lifetime; callback must not subscribe recursively. */
	struct mutex event_lock;
	void (*event)(void *);
	void *event_data;
};
static inline struct bq25970_device *to_bq25970_device(struct auxiliary_device *aux)
{
	return container_of(aux, struct bq25970_device, aux);
}
int bq25970_subscribe(struct bq25970_device *bq, void (*event)(void *), void *data);
void bq25970_unsubscribe(struct bq25970_device *bq);
#endif

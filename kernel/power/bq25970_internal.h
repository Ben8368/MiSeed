/* SPDX-License-Identifier: GPL-2.0-only */
/* Private hardware state; never include from the charging coordinator. */
#ifndef MISEED_BQ25970_INTERNAL_H
#define MISEED_BQ25970_INTERNAL_H
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include "bq25970.h"
#include "bq25970_device.h"
#define BQ25970_MAX_PROTECTION 20
struct bq25970_device {
	struct device *dev;
	struct regmap *map;
	struct mutex lock, event_lock;
	struct power_supply *psy;
	struct power_supply_desc desc;
	struct bq25970_reg protection[BQ25970_MAX_PROTECTION];
	unsigned int nprotection;
	struct bq25970_sample cached;
	bool configured, sample_valid, owner;
	int sample_error;
	void (*event)(void *);
	void *event_data;
};
int bq25970_register_supply(struct bq25970_device *bq);
#endif

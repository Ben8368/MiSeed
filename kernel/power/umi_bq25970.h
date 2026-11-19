/* SPDX-License-Identifier: GPL-2.0-only */
/* Legacy Umi DT adapter. Not part of the generic chip interface. */
#ifndef MISEED_UMI_BQ25970_H
#define MISEED_UMI_BQ25970_H
#include <linux/auxiliary_bus.h>
struct bq25970_device;
struct umi_bq25970 {
	struct auxiliary_device aux;
	struct bq25970_device *chip;
};
static inline struct bq25970_device *umi_bq25970_chip(struct auxiliary_device *aux)
{
	return container_of(aux, struct umi_bq25970, aux)->chip;
}
int umi_bq25970_profile(struct device *dev, struct bq25970_device *bq);
#endif

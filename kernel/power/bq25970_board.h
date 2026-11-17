/* SPDX-License-Identifier: GPL-2.0-only */
/* Migration-only board image input. The Umi hardware adapter is its sole
 * consumer. Runtime policy must include only bq25970_device.h. This interface
 * is not a DT binding or userspace ABI; replace it with qualified semantic
 * settings before proposing active charging support for other boards.
 */
#ifndef MISEED_BQ25970_BOARD_H
#define MISEED_BQ25970_BOARD_H
#include "bq25970.h"
struct bq25970_device;
int bq25970_set_board_image(struct bq25970_device *bq,
		const struct bq25970_reg *image, unsigned int count);
#endif

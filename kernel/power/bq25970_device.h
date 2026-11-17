/* SPDX-License-Identifier: GPL-2.0-only */
/* Opaque in-kernel pump interface. No PD, SMB5, thermal policy or regmap. */
#ifndef MISEED_BQ25970_DEVICE_H
#define MISEED_BQ25970_DEVICE_H
#include <linux/types.h>
struct bq25970_device;
struct i2c_client;
struct device;
struct bq25970_sample {
	/* Diagnostic bytes only; consumers must use decoded fields for control. */
	unsigned int fault, latched, alarm, status, watchdog, control;
	bool hardware_fault, alarm_active, bus_low, bus_high, switching, enabled;
	bool clean_disabled, input_present;
	int ibus_ua, vbus_uv, vbat_uv;
};
struct bq25970_device *bq25970_register(struct i2c_client *client);
int bq25970_stop(struct bq25970_device *bq);
int bq25970_start(struct bq25970_device *bq);
int bq25970_check_configuration(struct bq25970_device *bq);
int bq25970_prepare(struct bq25970_device *bq);
int bq25970_read_sample(struct bq25970_device *bq, struct bq25970_sample *sample);
int bq25970_is_switching(struct bq25970_device *bq, bool *switching);
/* Subscribe grants exclusive control. Stop/recover before unsubscribe.
 * Callback only schedules work; caller must drain it before destroying state.
 * The board adapter owns chip lifetime and removes its consumer first.
 */
int bq25970_subscribe(struct bq25970_device *bq, void (*event)(void *), void *data);
void bq25970_unsubscribe(struct bq25970_device *bq);
void bq25970_shutdown_device(struct bq25970_device *bq);
#endif

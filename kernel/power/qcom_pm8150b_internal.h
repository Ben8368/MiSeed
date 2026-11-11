// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Teguh Sobirin <teguh@sobir.in>.
 *
 * This driver is for the switch-mode battery charger power delivery
 * and boost hardware found in pm8150b and related PMICs.
 * This work based on pmi8998 charger driver by
 * Caleb Connolly <caleb.connolly@linaro.org>
 * Should be merged with the existing charger driver in the future.
 */

/* MiSeed: Umi runtime integration. Standard CHARGE_BEHAVIOUR API,
 * fail-closed polling, LOS-derived limits and automatic charge policy.
 */

#ifndef MISEED_QCOM_PM8150B_INTERNAL_H
#define MISEED_QCOM_PM8150B_INTERNAL_H

#include <linux/bits.h>
#include <linux/devm-helpers.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>
#include <linux/thermal.h>
#include "umi_charge_policy.h"
#include "umi_smb5.h"

#include "qcom_pm8150b_regs.h"

enum charger_status {
 INHIBIT_CHARGE = 0,
 TRICKLE_CHARGE,
 PRE_CHARGE,
 FULLON_CHARGE,
 TAPER_CHARGE,
 TERMINATE_CHARGE,
 PAUSE_CHARGE,
 DISABLE_CHARGE,
};

struct smb5_register {
	u16 addr;
	u8 mask;
	u8 val;
};

/**
 * struct smb5_chip - smb5 chip structure
 * @dev:		Device reference for power_supply
 * @name:		The platform device name
 * @base:		Base address for smb5 registers
 * @regmap:		Register map
 * @batt_info:		Battery data from DT
 * @status_change_work: Worker to handle plug/unplug events
 * @cable_irq:		USB plugin IRQ
 * @wakeup_enabled:	If the cable IRQ will cause a wakeup
 * @usb_in_i_chan:	USB_IN current measurement channel
 * @usb_in_v_chan:	USB_IN voltage measurement channel
 * @chg_psy:		Charger power supply instance
 */
struct smb5_chip {
	struct device *dev;
	const char *name;
	unsigned int base;
	struct regmap *regmap;
	struct power_supply_battery_info *batt_info;

	struct delayed_work status_change_work;
	int cable_irq;
	bool wakeup_enabled;

	struct iio_channel *usb_in_i_chan;
	struct iio_channel *usb_in_v_chan;

	struct power_supply *chg_psy;
	struct power_supply *bms;
	struct mutex lock;
	bool enabled, inhibit, stopping, test_armed, test_saw_disconnect, test_active;
	bool pps_owned;
	bool full_bypass;
	int power_stage;
	unsigned int policy_icl_ua, saved_icl_ua;
	unsigned long cooling_state;
	unsigned long test_deadline;
	unsigned int test_seconds;
	int max_fv_uv;
};


int smb5_get_prop_usb_online(struct smb5_chip *chip, int *val);
int smb5_apsd_get_charger_type(struct smb5_chip *chip, int *val);
int smb5_get_prop_charging_enabled(struct smb5_chip *chip, int *val);
int smb5_get_prop_status(struct smb5_chip *chip, int *val);
int smb5_get_current_limit(struct smb5_chip *chip,
					 unsigned int *val);
int smb5_set_current_limit(struct smb5_chip *chip, unsigned int val);
int smb5_set_prop_charging_enabled(struct smb5_chip *chip, unsigned int val);
int smb5_inhibit_with_fallback(struct smb5_chip *chip, const char *where);
int smb5_get_iio_chan(struct smb5_chip *chip, struct iio_channel *chan,
			     int *val);
int smb5_get_input_current(struct smb5_chip *chip, int *current_ua);
int smb5_get_prop_health(struct smb5_chip *chip, int *val);
int smb5_init_hw(struct smb5_chip *chip);
void smb5_status_change_work(struct work_struct *work);
int smb5_register_thermal(struct smb5_chip *chip);
int smb5_set_property(struct power_supply *psy,
			     enum power_supply_property psp,
			     const union power_supply_propval *val);
extern const struct power_supply_desc smb5_psy_desc;
#ifdef CONFIG_MISEED_CHARGE_DIAGNOSTICS
extern const struct attribute_group smb5_group;
#endif

#endif

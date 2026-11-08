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
 * fail-closed polling, source-derived limits, bounded single-insertion test.
 */

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
#include "umi_charge_policy.h"

/* clang-format off */
#define BATTERY_CHARGER_STATUS_1			0x06
#define ICL_INCR_REQ_FOR_PRECHG_BIT			BIT(7)
#define ZERO_CHARGE_CURRENT_BIT				BIT(6)
#define STEP_CHARGING_STATUS_SHIFT			3
#define STEP_CHARGING_STATUS_MASK			GENMASK(5, 3)
#define BATTERY_CHARGER_STATUS_MASK			GENMASK(2, 0)

#define BATTERY_CHARGER_STATUS_2			0x07
#define DROP_IN_BATTERY_VOLTAGE_REFERENCE_BIT		BIT(7)
#define VBATT_LTET_RECHARGE_BIT				BIT(6)
#define VBATT_GTET_INHIBIT_BIT				BIT(5)
#define VBATT_GTET_FLOAT_VOLTAGE_BIT			BIT(4)
#define BATT_GT_FULL_ON_BIT				BIT(3)
#define CHARGER_ERROR_STATUS_SFT_EXPIRE_BIT		BIT(2)
#define CHARGER_ERROR_STATUS_BAT_OV_BIT			BIT(1)
#define CHARGER_ERROR_STATUS_BAT_TERM_MISSING_BIT	BIT(0)

#define BATTERY_CHARGER_STATUS_4		0x0A
#define CHARGE_CURRENT_REFERENCE_MASK		GENMASK(7, 0)

#define BATTERY_CHARGER_STATUS_7_REG		0x0D
#define BAT_TEMP_STATUS_SOFT_LIMIT_MASK		GENMASK(5, 4)
#define BAT_TEMP_STATUS_HOT_SOFT_BIT		BIT(5)
#define BAT_TEMP_STATUS_COLD_SOFT_BIT		BIT(4)
#define BAT_TEMP_STATUS_HARD_LIMIT_MASK		GENMASK(3, 2)
#define BAT_TEMP_STATUS_TOO_HOT_BIT		BIT(3)
#define BAT_TEMP_STATUS_TOO_COLD_BIT		BIT(2)
#define BAT_TEMP_STATUS_TOO_HOT_AFP_BIT		BIT(1)
#define BAT_TEMP_STATUS_TOO_COLD_AFP_BIT	BIT(0)

#define CHARGING_ENABLE_CMD			0x42
#define CHARGING_ENABLE_CMD_BIT			BIT(0)
#define CHARGING_ENABLE_POF_BIT			BIT(1)

#define CHGR_CFG2				0x51
#define EN_FAVOR_IN_BIT				BIT(5)
#define BAT_OV_ECC_BIT				BIT(4)
#define I_TERM_BIT				BIT(3)
#define AUTO_RECHG_BIT				BIT(2)
#define SOC_BASED_RECHG_BIT			BIT(1)
#define CHARGER_INHIBIT_BIT			BIT(0)

#define PRE_CHARGE_CURRENT_CFG			0x60
#define PRE_CHARGE_CURRENT_SETTING_MASK		GENMASK(2, 0)

#define FAST_CHARGE_CURRENT_CFG			0x61
#define FAST_CHARGE_CURRENT_SETTING_MASK	GENMASK(7, 0)

#define FLOAT_VOLTAGE_CFG			0x70
#define FLOAT_VOLTAGE_SETTING_MASK		GENMASK(7, 0)

#define CHARGE_RCHG_SOC_THRESHOLD_CFG_REG	0x7D
#define CHARGE_RCHG_SOC_THRESHOLD_CFG_MASK	GENMASK(7, 0)

#define ICL_STATUS				0x107
#define ICL_INPUT_CURRENT_LIMIT_MASK		GENMASK(7, 0)

#define AICL_STATUS				0x108
#define AICL_INPUT_CURRENT_LIMIT_MASK		GENMASK(7, 0)

#define POWER_PATH_STATUS				0x10B
#define P_PATH_INPUT_SS_DONE_BIT			BIT(7)
#define P_PATH_USBIN_SUSPEND_STS_BIT			BIT(6)
#define P_PATH_DCIN_SUSPEND_STS_BIT			BIT(5)
#define P_PATH_USE_USBIN_BIT				BIT(4)
#define P_PATH_USE_DCIN_BIT				BIT(3)
#define P_PATH_POWER_PATH_MASK				GENMASK(2, 1)
#define P_PATH_VALID_INPUT_POWER_SOURCE_STS_BIT		BIT(0)

#define OTG_CFG					0x153
#define OTG_RESERVED_BIT			BIT(7)
#define FAST_ROLE_SWAP_START_OPTION_BIT		BIT(6)
#define DIS_OTG_ON_TSD_BIT			BIT(5)
#define OTG_CFG_4_BIT				BIT(4)
#define EN_SOC_BASED_OTG_UVLO_BIT		BIT(3)
#define ENABLE_OTG_IN_DEBUG_MODE_BIT		BIT(2)
#define OTG_EN_SRC_CFG_BIT			BIT(1)
#define OTG_HICCUP_CNTR_RST_TIMER_SEL_BIT	BIT(0)

#define APSD_STATUS				0x307
#define APSD_STATUS_7_BIT			BIT(7)
#define HVDCP_CHECK_TIMEOUT_BIT			BIT(6)
#define SLOW_PLUGIN_TIMEOUT_BIT			BIT(5)
#define ENUMERATION_DONE_BIT			BIT(4)
#define VADP_CHANGE_DONE_AFTER_AUTH_BIT		BIT(3)
#define QC_AUTH_DONE_STATUS_BIT			BIT(2)
#define QC_CHARGER_BIT				BIT(1)
#define APSD_DTC_STATUS_DONE_BIT		BIT(0)

#define APSD_RESULT_STATUS			0x308
#define APSD_RESULT_STATUS_7_BIT		BIT(7)
#define APSD_RESULT_STATUS_MASK			GENMASK(6, 0)
#define QC_3P0_BIT				BIT(6)
#define QC_2P0_BIT				BIT(5)
#define FLOAT_CHARGER_BIT			BIT(4)
#define DCP_CHARGER_BIT				BIT(3)
#define CDP_CHARGER_BIT				BIT(2)
#define OCP_CHARGER_BIT				BIT(1)
#define SDP_CHARGER_BIT				BIT(0)

#define USBIN_INT_RT_STS_OFFSET			0x310
#define USBIN_PLUGIN_RT_STS_BIT			BIT(4)

#define USBIN_CMD_IL				0x340
#define USBIN_SUSPEND_BIT			BIT(0)

#define CMD_APSD				0x341
#define APSD_RERUN_BIT				BIT(0)

#define CMD_ICL_OVERRIDE			0x342
#define ICL_OVERRIDE_BIT			BIT(0)

#define TYPE_C_CFG				0x358
#define BC1P2_START_ON_CC_BIT			BIT(7)

#define HVDCP_PULSE_COUNT_MAX			0x35b
#define HVDCP_PULSE_COUNT_MAX_QC2_MASK		GENMASK(7, 6)

#define USBIN_ADAPTER_ALLOW_CFG			0x360
#define USBIN_ADAPTER_ALLOW_MASK		GENMASK(3, 0)

#define USBIN_OPTIONS_1_CFG			0x362
#define HVDCP_AUTH_ALG_EN_CFG_BIT		BIT(6)
#define HVDCP_AUTONOMOUS_MODE_EN_CFG_BIT	BIT(5)
#define BC1P2_SRC_DETECT_BIT			BIT(3)
#define HVDCP_EN_BIT				BIT(2)
#define HVDCP_NO_AUTH_QC3_CFG_BIT		BIT(1)

#define USBIN_LOAD_CFG				0x365
#define ICL_OVERRIDE_AFTER_APSD_BIT		BIT(4)
#define USBIN_AICL_STEP_TIMING_SEL_MASK		GENMASK(3, 2)
#define USBIN_IN_COLLAPSE_GF_SEL_MASK		GENMASK(1, 0)

#define USBIN_ICL_OPTIONS			0x366
#define CFG_USB3P0_SEL_BIT			BIT(2)
#define USB51_MODE_BIT				BIT(1)
#define USBIN_MODE_CHG_BIT			BIT(0)

#define USBIN_CURRENT_LIMIT_CFG			0x370
#define USBIN_CURRENT_LIMIT_MASK		GENMASK(7, 0)

#define USBIN_AICL_OPTIONS_CFG			0x380
#define SUSPEND_ON_COLLAPSE_USBIN_BIT		BIT(7)
#define USBIN_AICL_PERIODIC_RERUN_EN_BIT	BIT(4)
#define USBIN_AICL_ADC_EN_BIT			BIT(3)
#define USBIN_AICL_EN_BIT			BIT(2)

#define USBIN_5V_AICL_THRESHOLD_CFG		0x381
#define USBIN_5V_AICL_THRESHOLD_CFG_MASK	GENMASK(2, 0)

#define USBIN_CONT_AICL_THRESHOLD_CFG		0x384
#define USBIN_CONT_AICL_THRESHOLD_CFG_MASK	GENMASK(5, 0)

#define DCIN_CMD_IL				0x440
#define DCIN_SUSPEND_BIT			BIT(0)

#define TYPE_C_SNK_STATUS			0x506
#define DETECTED_SRC_TYPE_MASK			GENMASK(6, 0)
#define SNK_RP_STD_DAM_BIT			BIT(6)
#define SNK_RP_1P5_DAM_BIT			BIT(5)
#define SNK_RP_3P0_DAM_BIT			BIT(4)
#define SNK_DAM_MASK				GENMASK(6, 4)
#define SNK_DAM_500MA_BIT			BIT(6)
#define SNK_DAM_1500MA_BIT			BIT(5)
#define SNK_DAM_3000MA_BIT			BIT(4)
#define SNK_RP_STD_BIT				BIT(3)
#define SNK_RP_1P5_BIT				BIT(2)
#define SNK_RP_3P0_BIT				BIT(1)
#define SNK_RP_SHORT_BIT			BIT(0)

#define TYPE_C_MODE_CFG				0x544
#define TYPEC_TRY_MODE_MASK			GENMASK(4, 3)
#define EN_TRY_SNK_BIT				BIT(4)
#define EN_TRY_SRC_BIT				BIT(3)
#define TYPEC_POWER_ROLE_CMD_MASK		GENMASK(2, 0)
#define EN_SRC_ONLY_BIT				BIT(2)
#define EN_SNK_ONLY_BIT				BIT(1)
#define TYPEC_DISABLE_CMD_BIT			BIT(0)

#define TYPEC_TYPE_C_VCONN_CONTROL		0x546
#define VCONN_EN_ORIENTATION_BIT		BIT(2)
#define VCONN_EN_VALUE_BIT			BIT(1)
#define VCONN_EN_SRC_BIT			BIT(0)

#define TYPE_C_DEBUG_ACCESS_SINK		0x54a
#define TYPEC_DEBUG_ACCESS_SINK_MASK		GENMASK(4, 0)

#define DEBUG_ACCESS_SRC_CFG			0x54C
#define EN_UNORIENTED_DEBUG_ACCESS_SRC_BIT	BIT(0)

#define TYPE_C_EXIT_STATE_CFG			0x550
#define BYPASS_VSAFE0V_DURING_ROLE_SWAP_BIT	BIT(3)
#define SEL_SRC_UPPER_REF_BIT			BIT(2)
#define EXIT_SNK_BASED_ON_CC_BIT		BIT(0)

#define TYPE_C_INTERRUPT_EN_CFG_1			0x55e
#define TYPEC_LEGACY_CABLE_INT_EN_BIT			BIT(7)
#define TYPEC_NONCOMPLIANT_LEGACY_CABLE_INT_EN_BIT	BIT(6)
#define TYPEC_TRYSOURCE_DETECT_INT_EN_BIT		BIT(5)
#define TYPEC_TRYSINK_DETECT_INT_EN_BIT			BIT(4)
#define TYPEC_CCOUT_DETACH_INT_EN_BIT			BIT(3)
#define TYPEC_CCOUT_ATTACH_INT_EN_BIT			BIT(2)
#define TYPEC_VBUS_DEASSERT_INT_EN_BIT			BIT(1)
#define TYPEC_VBUS_ASSERT_INT_EN_BIT			BIT(0)

#define BARK_BITE_WDOG_PET			0x643
#define BARK_BITE_WDOG_PET_BIT			BIT(0)

#define WD_CFG					0x651
#define WATCHDOG_TRIGGER_AFP_EN_BIT		BIT(7)
#define BARK_WDOG_INT_EN_BIT			BIT(6)
#define BITE_WDOG_INT_EN_BIT			BIT(5)
#define SFT_AFTER_WDOG_IRQ_MASK			GENMASK(4, 3)
#define WDOG_IRQ_SFT_BIT			BIT(2)
#define WDOG_TIMER_EN_ON_PLUGIN_BIT		BIT(1)
#define WDOG_TIMER_EN_BIT			BIT(0)

#define SNARL_BARK_BITE_WD_CFG			0x653
#define BITE_WDOG_DISABLE_CHARGING_CFG_BIT	BIT(7)
#define SNARL_WDOG_TIMEOUT_MASK			GENMASK(6, 4)
#define BARK_WDOG_TIMEOUT_MASK			GENMASK(3, 2)
#define BITE_WDOG_TIMEOUT_MASK			GENMASK(1, 0)

#define AICL_RERUN_TIME_CFG			0x661
#define AICL_RERUN_TIME_MASK			GENMASK(1, 0)

#define SDP_CURRENT_UA			500000
#define CDP_CURRENT_UA			3000000
#define DCP_CURRENT_UA			3300000
#define CURRENT_MAX_UA			DCP_CURRENT_UA

/* pmi8150b registers represent current in increments of 1/40th of an amp */
#define CURRENT_SCALE_FACTOR		50000
/* clang-format on */

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
	unsigned long test_deadline;
	unsigned int test_seconds;
	int max_fv_uv;
};

static enum power_supply_property smb5_properties[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_START_THRESHOLD,
};

static int smb5_get_prop_usb_online(struct smb5_chip *chip, int *val)
{
	unsigned int stat;
	int rc;

	rc = regmap_read(chip->regmap, chip->base + USBIN_INT_RT_STS_OFFSET, &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read USBIN_RT_STS rc=%d\n", rc);
		return rc;
	}

	*val = (stat & USBIN_PLUGIN_RT_STS_BIT) ? 1 : 0;
	return 0;
}

/*
 * Qualcomm "automatic power source detection" aka APSD
 * tells us what type of charger we're connected to.
 */
static int smb5_apsd_get_charger_type(struct smb5_chip *chip, int *val)
{
	unsigned int apsd_stat, stat;
	int usb_online = 0;
	int rc;

	rc = smb5_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		*val = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		return rc;
	}

	rc = regmap_read(chip->regmap, chip->base + APSD_STATUS, &apsd_stat);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read apsd status, rc = %d", rc);
		return rc;
	}
	if (!(apsd_stat & APSD_DTC_STATUS_DONE_BIT)) {
		dev_err(chip->dev, "Apsd not ready");
		return -EAGAIN;
	}

	rc = regmap_read(chip->regmap, chip->base + APSD_RESULT_STATUS, &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read apsd result, rc = %d", rc);
		return rc;
	}

	stat &= APSD_RESULT_STATUS_MASK;

	if (stat & CDP_CHARGER_BIT){
		*val = POWER_SUPPLY_USB_TYPE_CDP;
	} else if (stat & DCP_CHARGER_BIT){
		*val = POWER_SUPPLY_USB_TYPE_DCP;
	} else if (stat & OCP_CHARGER_BIT){
		*val = POWER_SUPPLY_USB_TYPE_DCP;
	} else if (stat & FLOAT_CHARGER_BIT){
		*val = POWER_SUPPLY_USB_TYPE_DCP;
	} else if (stat & QC_2P0_BIT){
		*val = POWER_SUPPLY_USB_TYPE_DCP;
	} else if (stat & QC_3P0_BIT){
		*val = POWER_SUPPLY_USB_TYPE_DCP;
	} else {
		*val = POWER_SUPPLY_USB_TYPE_SDP;
	}

	return 0;
}

static int smb5_get_prop_charging_enabled(struct smb5_chip *chip, int *val)
{
	unsigned int charging_enabled;
	int rc = regmap_read(chip->regmap, chip->base + CHARGING_ENABLE_CMD, &charging_enabled);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read charging enabled status, rc = %d", rc);
		return rc;
	}
	*val = charging_enabled & CHARGING_ENABLE_CMD_BIT ?
		POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO :
		POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE;
	return 0;
}

static int smb5_get_prop_status(struct smb5_chip *chip, int *val)
{
	unsigned char stat[2];
	int usb_online = 0;
	int rc;

	rc = smb5_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		*val = POWER_SUPPLY_STATUS_DISCHARGING;
		return rc;
	}

	rc = regmap_bulk_read(chip->regmap,
			      chip->base + BATTERY_CHARGER_STATUS_1, &stat, 2);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read charging status ret=%d\n",
			rc);
		return rc;
	}

	if (stat[1] & VBATT_GTET_INHIBIT_BIT) {
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return 0;
	}

	stat[0] = stat[0] & BATTERY_CHARGER_STATUS_MASK;

	switch (stat[0]) {
	case TRICKLE_CHARGE:
	case PRE_CHARGE:
	case FULLON_CHARGE:
	case TAPER_CHARGE:
		*val = POWER_SUPPLY_STATUS_CHARGING;
		return rc;
	case INHIBIT_CHARGE:
	case PAUSE_CHARGE:
	case DISABLE_CHARGE:
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return rc;
	case TERMINATE_CHARGE:
		*val = POWER_SUPPLY_STATUS_FULL;
		return rc;
	default:
		*val = POWER_SUPPLY_STATUS_UNKNOWN;
		return rc;
	}
}

static inline int smb5_get_current_limit(struct smb5_chip *chip,
					 unsigned int *val)
{
	int rc = regmap_read(chip->regmap, chip->base + AICL_STATUS, val);

	if (rc >= 0)
		*val *= CURRENT_SCALE_FACTOR;
	return rc;
}

static int smb5_set_current_limit(struct smb5_chip *chip, unsigned int val)
{
	unsigned char val_raw;

	if (val > UMI_DIAGNOSTIC_CURRENT_UA) {
		dev_err(chip->dev,
			"Diagnostic input current cap exceeded");
		return -EINVAL;
	}
	val_raw = val / CURRENT_SCALE_FACTOR;

	return regmap_write(chip->regmap, chip->base + USBIN_CURRENT_LIMIT_CFG,
		val_raw);
}

static int smb5_set_prop_charging_enabled(struct smb5_chip *chip, unsigned int val)
{
	if (val != POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO &&
	    val != POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE)
		return -EINVAL;

	return regmap_update_bits(chip->regmap, chip->base + CHARGING_ENABLE_CMD,
				 CHARGING_ENABLE_CMD_BIT,
				 val == POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO ?
				 CHARGING_ENABLE_CMD_BIT : 0);
}

static int smb5_inhibit_with_fallback(struct smb5_chip *chip, const char *where)
{
	int rc, suspend_rc;

	rc = smb5_set_prop_charging_enabled(chip,
			POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE);
	if (rc) {
		suspend_rc = regmap_update_bits(chip->regmap,
			chip->base + USBIN_CMD_IL, USBIN_SUSPEND_BIT,
			USBIN_SUSPEND_BIT);
		dev_err(chip->dev,
			"miseed-charge %s stop failed rc=%d input_stop_rc=%d; watchdog not fed\n",
			where, rc, suspend_rc);
	}

	return rc;
}

static int smb5_get_prop_health(struct smb5_chip *chip, int *val);

static int smb5_battery_value(struct smb5_chip *chip,
                             enum power_supply_property property, int *value)
{
 union power_supply_propval val;
 int rc = power_supply_get_property(chip->bms, property, &val);
 if (!rc)
  *value = val.intval;
 return rc;
}

static int smb5_limits_verified(struct smb5_chip *chip)
{
 static const unsigned int regs[] = { FAST_CHARGE_CURRENT_CFG, USBIN_CURRENT_LIMIT_CFG,
                                     FLOAT_VOLTAGE_CFG, WD_CFG, SNARL_BARK_BITE_WD_CFG, 0x670 };
 unsigned int v[ARRAY_SIZE(regs)];
 int rc, i;
 for (i = 0; i < ARRAY_SIZE(regs); i++) {
  rc = regmap_read(chip->regmap, chip->base + regs[i], &v[i]);
  if (rc) return rc;
 }
 if (v[0] != UMI_DIAGNOSTIC_CURRENT_UA / CURRENT_SCALE_FACTOR ||
     v[1] > UMI_DIAGNOSTIC_CURRENT_UA / CURRENT_SCALE_FACTOR ||
     3600000 + (int)v[2] * 10000 != chip->max_fv_uv ||
     !(v[3] & WDOG_TIMER_EN_BIT) ||
     !(v[4] & BITE_WDOG_DISABLE_CHARGING_CFG_BIT) || !(v[5] & BIT(0)))
  return -EPERM;
 return 0;
}

static void smb5_status_change_work(struct work_struct *work)
{
 struct smb5_chip *chip = container_of(work, struct smb5_chip, status_change_work.work);
 int online = 0, present = 0, health = POWER_SUPPLY_HEALTH_UNKNOWN;
 int temp = -10000, voltage = -1, soc = -1, bms_current = 0;
 int rc, usb_rc, allow = 0, stop_rc;
 mutex_lock(&chip->lock);
 if (chip->stopping) {
  mutex_unlock(&chip->lock);
  return;
 }
 usb_rc = smb5_get_prop_usb_online(chip, &online);
 rc = usb_rc;
 if (!rc) rc = smb5_battery_value(chip, POWER_SUPPLY_PROP_PRESENT, &present);
 if (!rc) rc = smb5_battery_value(chip, POWER_SUPPLY_PROP_TEMP, &temp);
 if (!rc) rc = smb5_battery_value(chip, POWER_SUPPLY_PROP_VOLTAGE_NOW, &voltage);
 if (!rc) rc = smb5_battery_value(chip, POWER_SUPPLY_PROP_CAPACITY, &soc);
 if (!rc) rc = smb5_battery_value(chip, POWER_SUPPLY_PROP_CURRENT_NOW, &bms_current);
 if (!rc) rc = smb5_get_prop_health(chip, &health);
 if (!rc) rc = smb5_limits_verified(chip);
 if (!rc && bms_current < -UMI_DIAGNOSTIC_CURRENT_UA) rc = -EOVERFLOW;
 if (!usb_rc && !online) {
  if (chip->test_active) {
   chip->test_active = false;
   chip->test_armed = false;
  } else if (chip->test_armed) {
   chip->test_saw_disconnect = true;
  }
 }
 if (!rc && online && chip->test_armed && chip->test_saw_disconnect && !chip->test_active) {
  chip->test_active = true;
  chip->test_deadline = jiffies + chip->test_seconds * HZ;
  dev_info(chip->dev, "miseed-charge test window started: %u seconds maximum\n", chip->test_seconds);
 }
 if (chip->test_active && time_after_eq(jiffies, chip->test_deadline)) {
  chip->test_active = false;
  chip->test_armed = false;
 }
 if (!rc && !chip->inhibit && (!chip->test_armed || chip->test_active))
  allow = umi_charge_policy(online, present, health == POWER_SUPPLY_HEALTH_GOOD,
                           temp, voltage, soc, chip->max_fv_uv,
                           chip->enabled, chip->test_active);
 if (allow) {
  /* Limits are set with charging disabled, before probe registers IRQ/work. */
  rc = regmap_update_bits(chip->regmap, chip->base + USBIN_CMD_IL, USBIN_SUSPEND_BIT, 0);
  if (!rc) rc = regmap_write(chip->regmap, chip->base + BARK_BITE_WDOG_PET, BARK_BITE_WDOG_PET_BIT);
  if (!rc) rc = smb5_set_prop_charging_enabled(chip, POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO);
 }
 if (!allow || rc) {
  stop_rc = smb5_inhibit_with_fallback(chip, "worker");
  if (!rc) rc = stop_rc;
 }
 if (rc && chip->test_armed) {
  chip->test_active = false;
  chip->test_armed = false;
  chip->inhibit = true;
 }
 chip->enabled = allow && !rc;
 dev_info(chip->dev, "miseed-charge enabled=%u input=%d present=%d health=%d temp_decic=%d vbat_uv=%d current_raw_ua=%d soc=%d test_armed=%u test_active=%u rc=%d fcc_ua=%u icl_ua=%u fv_uv=%d\n",
          chip->enabled, online, present, health, temp, voltage, bms_current, soc,
          chip->test_armed, chip->test_active, rc,
          UMI_DIAGNOSTIC_CURRENT_UA, UMI_DIAGNOSTIC_CURRENT_UA, chip->max_fv_uv);
 mutex_unlock(&chip->lock);
 power_supply_changed(chip->chg_psy);
 schedule_delayed_work(&chip->status_change_work, msecs_to_jiffies(1000));
}

static int smb5_get_iio_chan(struct smb5_chip *chip, struct iio_channel *chan,
			     int *val)
{
	int rc;
	int online;

	if (IS_ERR(chan)) {
		dev_err(chip->dev, "Failed to get IIO channel, err = %li\n", PTR_ERR(chan));
		return PTR_ERR(chan);
	}

	rc = smb5_get_prop_usb_online(chip, &online);
	if (rc < 0)
		return rc;
	if (!online) {
		*val = 0;
		return 0;
	}

	return iio_read_channel_processed(chan, val);
}

static int smb5_get_prop_health(struct smb5_chip *chip, int *val)
{
	int rc;
	unsigned int stat;

	rc = regmap_read(chip->regmap, chip->base + BATTERY_CHARGER_STATUS_2,
			 &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read charger status rc=%d\n", rc);
		return rc;
	}

	if (stat & CHARGER_ERROR_STATUS_BAT_OV_BIT) {
		dev_err(chip->dev, "battery over-voltage\n");
		*val = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		return 0;
	}

	rc = regmap_read(chip->regmap, chip->base + BATTERY_CHARGER_STATUS_7_REG,
			 &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read charger status 7 rc=%d\n", rc);
		return rc;
	}

	if (stat & (BAT_TEMP_STATUS_TOO_COLD_BIT | BAT_TEMP_STATUS_TOO_COLD_AFP_BIT))
		*val = POWER_SUPPLY_HEALTH_COLD;
	else if (stat & (BAT_TEMP_STATUS_TOO_HOT_BIT | BAT_TEMP_STATUS_TOO_HOT_AFP_BIT))
		*val = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (stat & BAT_TEMP_STATUS_COLD_SOFT_BIT)
		*val = POWER_SUPPLY_HEALTH_COOL;
	else if (stat & BAT_TEMP_STATUS_HOT_SOFT_BIT)
		*val = POWER_SUPPLY_HEALTH_WARM;
	else
		*val = POWER_SUPPLY_HEALTH_GOOD;

	return 0;
}

static int smb5_get_charge_control_threshold(struct smb5_chip *chip, bool end)
{
	int threshold;

	mutex_lock(&chip->lock);
	if (chip->test_active)
		threshold = end ? 100 : 0;
	else
		threshold = end ? UMI_CHARGE_UPPER_SOC : UMI_CHARGE_LOWER_SOC;
	mutex_unlock(&chip->lock);

	return threshold;
}

static int smb5_get_property(struct power_supply *psy,
			     enum power_supply_property psp,
			     union power_supply_propval *val)
{
	struct smb5_chip *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Qualcomm";
		return 0;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = chip->name;
		return 0;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return smb5_get_current_limit(chip, &val->intval);
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return smb5_get_iio_chan(chip, chip->usb_in_i_chan,
					 &val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return smb5_get_iio_chan(chip, chip->usb_in_v_chan,
					 &val->intval);
	case POWER_SUPPLY_PROP_ONLINE:
		return smb5_get_prop_usb_online(chip, &val->intval);
	case POWER_SUPPLY_PROP_STATUS:
		return smb5_get_prop_status(chip, &val->intval);
	case POWER_SUPPLY_PROP_HEALTH:
		return smb5_get_prop_health(chip, &val->intval);
	case POWER_SUPPLY_PROP_USB_TYPE:
		return smb5_apsd_get_charger_type(chip, &val->intval);
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD:
		val->intval = smb5_get_charge_control_threshold(chip, true);
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_START_THRESHOLD:
		val->intval = smb5_get_charge_control_threshold(chip, false);
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		return smb5_get_prop_charging_enabled(chip, &val->intval);
	default:
		dev_err(chip->dev, "invalid property: %d\n", psp);
		return -EINVAL;
	}
}

static int smb5_set_property(struct power_supply *psy,
			     enum power_supply_property psp,
			     const union power_supply_propval *val)
{
	struct smb5_chip *chip = power_supply_get_drvdata(psy);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return smb5_set_current_limit(chip, val->intval);
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		if (val->intval != POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO &&
                    val->intval != POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE)
                        return -EINVAL;
                mutex_lock(&chip->lock);
                chip->inhibit = val->intval == POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE;
                if (chip->inhibit)
                        rc = smb5_set_prop_charging_enabled(chip, val->intval);
                mutex_unlock(&chip->lock);
                mod_delayed_work(system_wq, &chip->status_change_work, 0);
                return rc;
	default:
		dev_err(chip->dev, "No setter for property: %d\n", psp);
		return -EINVAL;
	}
}

static int smb5_property_is_writable(struct power_supply *psy,
				     enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		return 1;
	default:
		return 0;
	}
}

static irqreturn_t smb5_handle_batt_overvoltage(int irq, void *data)
{
	struct smb5_chip *chip = data;
	unsigned int status;

	if (regmap_read(chip->regmap, chip->base + BATTERY_CHARGER_STATUS_2,
			&status) < 0) {
		mod_delayed_work(system_wq, &chip->status_change_work, 0);
		return IRQ_HANDLED;
	}

	if (status & CHARGER_ERROR_STATUS_BAT_OV_BIT) {
		/* The hardware stops charging automatically */
		dev_err(chip->dev, "battery overvoltage detected\n");
		power_supply_changed(chip->chg_psy);
		mod_delayed_work(system_wq, &chip->status_change_work, 0);
	}

	return IRQ_HANDLED;
}

static irqreturn_t smb5_handle_usb_plugin(int irq, void *data)
{
	struct smb5_chip *chip = data;
	int online = 0;
	int rc;

	/* Preserve short disconnects that could occur between worker samples. */
	rc = smb5_get_prop_usb_online(chip, &online);
	if (!rc && !online) {
		mutex_lock(&chip->lock);
		if (chip->test_armed && !chip->test_active)
			chip->test_saw_disconnect = true;
		mutex_unlock(&chip->lock);
	}

	power_supply_changed(chip->chg_psy);

	mod_delayed_work(system_wq, &chip->status_change_work, 0);

	return IRQ_HANDLED;
}

static irqreturn_t smb5_handle_usb_icl_change(int irq, void *data)
{
	struct smb5_chip *chip = data;

	power_supply_changed(chip->chg_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smb5_handle_wdog_bark(int irq, void *data)
{
 struct smb5_chip *chip = data;
 /* Only the worker may feed the watchdog, after a fresh safety sample. */
 mod_delayed_work(system_wq, &chip->status_change_work, 0);
 return IRQ_HANDLED;
}

static const struct power_supply_desc smb5_psy_desc = {
	.name = "pm8150b_charger",
	.charge_behaviours = BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO) |
			     BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE),
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_SDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_CDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_DCP) |
		     BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN),
	.properties = smb5_properties,
	.num_properties = ARRAY_SIZE(smb5_properties),
	.get_property = smb5_get_property,
	.set_property = smb5_set_property,
	.property_is_writeable = smb5_property_is_writable,
};


/* PM8150B encodings and watchdog semantics from the device's LOS qpnp-smb5.c.
 * Umi normal FV ceiling is 4.45 V; preserve any lower inherited FV. */
static int smb5_init_hw(struct smb5_chip *chip)
{
 unsigned int raw;
 int rc;
 rc = regmap_read(chip->regmap, chip->base + FLOAT_VOLTAGE_CFG, &raw);
 if (rc) return rc;
 chip->max_fv_uv = min(3600000 + (int)raw * 10000, UMI_CHARGE_MAX_UV);
 if (chip->max_fv_uv <= 3600000) return -ERANGE;
 rc = regmap_write(chip->regmap, chip->base + FLOAT_VOLTAGE_CFG,
                   (chip->max_fv_uv - 3600000) / 10000);
 if (rc) return rc;
 rc = regmap_write(chip->regmap, chip->base + FAST_CHARGE_CURRENT_CFG,
                   UMI_DIAGNOSTIC_CURRENT_UA / CURRENT_SCALE_FACTOR);
 if (rc) return rc;
 rc = smb5_set_current_limit(chip, UMI_DIAGNOSTIC_CURRENT_UA);
 if (rc) return rc;
 /* Keep inherited watchdog intervals; enable bite-to-stop and timer. */
 rc = regmap_update_bits(chip->regmap, chip->base + SNARL_BARK_BITE_WD_CFG,
                         BITE_WDOG_DISABLE_CHARGING_CFG_BIT,
                         BITE_WDOG_DISABLE_CHARGING_CFG_BIT);
 if (rc) return rc;
 rc = regmap_update_bits(chip->regmap, chip->base + WD_CFG,
                         WDOG_TIMER_EN_BIT | WDOG_TIMER_EN_ON_PLUGIN_BIT | BARK_WDOG_INT_EN_BIT,
                         WDOG_TIMER_EN_BIT | WDOG_TIMER_EN_ON_PLUGIN_BIT | BARK_WDOG_INT_EN_BIT);
 if (rc) return rc;
 /* Retain all other thermal settings, ensure the die shutdown comparator. */
 return regmap_update_bits(chip->regmap, chip->base + 0x670, BIT(0), BIT(0));
}

static ssize_t charge_test_show(struct device *dev, struct device_attribute *attr, char *buf)
{
 struct smb5_chip *chip = power_supply_get_drvdata(dev_get_drvdata(dev));
 ssize_t n;
 mutex_lock(&chip->lock);
 n = sysfs_emit(buf, "armed=%u active=%u seconds_max=%u\n",
                chip->test_armed, chip->test_active, chip->test_seconds);
 mutex_unlock(&chip->lock);
 return n;
}

static ssize_t charge_test_store(struct device *dev, struct device_attribute *attr,
                                 const char *buf, size_t count)
{
 struct smb5_chip *chip = power_supply_get_drvdata(dev_get_drvdata(dev));
 int rc;
 if (!sysfs_streq(buf, "arm") && !sysfs_streq(buf, "arm-10min") &&
     !sysfs_streq(buf, "stop")) return -EINVAL;
 mutex_lock(&chip->lock);
 chip->test_armed = !sysfs_streq(buf, "stop");
 chip->test_seconds = sysfs_streq(buf, "arm-10min") ?
                      UMI_CHARGE_EXTENDED_TEST_SECONDS : UMI_CHARGE_TEST_SECONDS;
 chip->inhibit = !chip->test_armed;
 chip->test_active = false;
 chip->test_saw_disconnect = false;
 chip->enabled = false;
 rc = smb5_inhibit_with_fallback(chip, "test-arm");
 if (rc) {
  chip->test_armed = false;
  chip->inhibit = true;
 }
 mutex_unlock(&chip->lock);
 if (rc) return rc;
 mod_delayed_work(system_wq, &chip->status_change_work, 0);
 return count;
}
static DEVICE_ATTR_RW(charge_test);
static struct attribute *smb5_attrs[] = { &dev_attr_charge_test.attr, NULL };
static const struct attribute_group smb5_group = { .attrs = smb5_attrs };

static void smb5_stop(void *data)
{
 struct smb5_chip *chip = data;

 mutex_lock(&chip->lock);
 chip->stopping = true;
 chip->inhibit = true;
 mutex_unlock(&chip->lock);
 cancel_delayed_work_sync(&chip->status_change_work);
	smb5_inhibit_with_fallback(chip, "teardown");
}

static int smb5_init_irq(struct smb5_chip *chip, int *irq, const char *name,
			 irqreturn_t (*handler)(int irq, void *data))
{
	int irqnum;
	int rc;

	irqnum = platform_get_irq_byname(to_platform_device(chip->dev), name);
	if (irqnum < 0)
		return irqnum;

	rc = devm_request_threaded_irq(chip->dev, irqnum, NULL, handler,
				       IRQF_ONESHOT, name, chip);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc, "Couldn't request irq %s\n",
				     name);

	if (irq)
		*irq = irqnum;

	return 0;
}

static int smb5_probe(struct platform_device *pdev)
{
	struct power_supply_config supply_config = {};
	struct power_supply_desc *desc;
	struct smb5_chip *chip;
	int rc, irq;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	chip->name = pdev->name;
	mutex_init(&chip->lock);
	/* First boot stays inhibited until telemetry is checked and a test is armed. */
	chip->inhibit = true;
	chip->test_seconds = UMI_CHARGE_TEST_SECONDS;

	chip->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chip->regmap)
		return dev_err_probe(chip->dev, -ENODEV,
				     "failed to locate the regmap\n");

	rc = device_property_read_u32(chip->dev, "reg", &chip->base);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc,
				     "Couldn't read base address\n");


	rc = smb5_inhibit_with_fallback(chip, "initial");
	if (rc)
		return rc;
 chip->bms = devm_power_supply_get_by_reference(chip->dev, "power-supplies");
 if (IS_ERR(chip->bms)) return PTR_ERR(chip->bms);
 if (!chip->bms) return -EPROBE_DEFER;
 rc = devm_delayed_work_autocancel(chip->dev, &chip->status_change_work, smb5_status_change_work);
 if (rc) return rc;

	chip->usb_in_v_chan = devm_iio_channel_get(chip->dev, "usb_in_v_div_16");
	if (IS_ERR(chip->usb_in_v_chan))
		return dev_err_probe(chip->dev, PTR_ERR(chip->usb_in_v_chan),
				     "Couldn't get usb_in_v_div_16 IIO channel\n");

	chip->usb_in_i_chan = devm_iio_channel_get(chip->dev, "usb_in_i_uv");
	if (IS_ERR(chip->usb_in_i_chan)) {
		return dev_err_probe(chip->dev, PTR_ERR(chip->usb_in_i_chan),
				     "Couldn't get usb_in_i_uv IIO channel\n");
	}


	supply_config.drv_data = chip;
	supply_config.fwnode = pdev->dev.fwnode;

	desc = devm_kzalloc(chip->dev, sizeof(smb5_psy_desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	memcpy(desc, &smb5_psy_desc, sizeof(smb5_psy_desc));
	desc->name =
		devm_kasprintf(chip->dev, GFP_KERNEL, "%s-charger",
			       (const char *)device_get_match_data(chip->dev));
	if (!desc->name)
		return -ENOMEM;

	chip->chg_psy =
		devm_power_supply_register(chip->dev, desc, &supply_config);
	if (IS_ERR(chip->chg_psy))
		return dev_err_probe(chip->dev, PTR_ERR(chip->chg_psy),
				     "failed to register power supply\n");

	rc = devm_add_action_or_reset(chip->dev, smb5_stop, chip);
	if (rc) return rc;

	rc = power_supply_get_battery_info(chip->chg_psy, &chip->batt_info);
	if (rc)
		return dev_err_probe(chip->dev, rc,
				     "Failed to get battery info\n");

	rc = smb5_init_hw(chip);
	if (rc) return rc;

	rc = smb5_init_irq(chip, &irq, "bat-ov", smb5_handle_batt_overvoltage);
	if (rc < 0)
		return rc;

	rc = smb5_init_irq(chip, &chip->cable_irq, "usbin-plugin",
			   smb5_handle_usb_plugin);
	if (rc < 0)
		return rc;

	rc = smb5_init_irq(chip, &irq, "usbin-icl-change",
			   smb5_handle_usb_icl_change);
	if (rc < 0)
		return rc;
	rc = smb5_init_irq(chip, &irq, "wdog-bark", smb5_handle_wdog_bark);
	if (rc < 0)
		return rc;

	rc = dev_pm_set_wake_irq(chip->dev, chip->cable_irq);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc, "Couldn't set wake irq\n");

	platform_set_drvdata(pdev, chip);
 rc = devm_device_add_group(&chip->chg_psy->dev, &smb5_group);
 if (rc) return rc;


	/* Initialise charger state */
	schedule_delayed_work(&chip->status_change_work, 0);

	return 0;
}

static void smb5_shutdown(struct platform_device *pdev)
{
 struct smb5_chip *chip = platform_get_drvdata(pdev);
 if (chip) smb5_stop(chip);
}

static const struct of_device_id smb5_match_id_table[] = {
	{ .compatible = "qcom,pm8150b-charger", .data = "pm8150b" },
	{ /* sentinal */ }
};
MODULE_DEVICE_TABLE(of, smb5_match_id_table);

static struct platform_driver qcom_spmi_smb5 = {
	.probe = smb5_probe,
	.shutdown = smb5_shutdown,
	.driver = {
		.name = "qcom-pm8150b-charger",
		.of_match_table = smb5_match_id_table,
		},
};

module_platform_driver(qcom_spmi_smb5);

MODULE_AUTHOR("Caleb Connolly <caleb.connolly@linaro.org>");
MODULE_DESCRIPTION("Qualcomm SMB5 Charger Driver");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020, The Linux Foundation. All rights reserved. */

#ifndef MISEED_QCOM_FG_INTERNAL_H
#define MISEED_QCOM_FG_INTERNAL_H

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/workqueue.h>

/* MiSeed: Linux 6.18 provides devm_power_supply_get_by_reference(). */

/* SOC */
#define BATT_MONOTONIC_SOC		0x009

/* BATT */
#define PARAM_ADDR_BATT_TEMP		0x150
#define BATT_INFO_JEITA_COLD		0x162
#define BATT_INFO_JEITA_COOL		0x163
#define BATT_INFO_JEITA_WARM		0x164
#define BATT_INFO_JEITA_HOT		0x165
#define PARAM_ADDR_BATT_VOLTAGE		0x1a0
#define PARAM_ADDR_BATT_CURRENT		0x1a2

/* RRADC */
#define ADC_RR_BATT_TEMP_LSB		0x288

/* MEMIF */
#define MEM_INTF_STS(chip)			(chip->ops->memif_base + 0x10)
#define MEM_INTF_CFG(chip)			(chip->ops->memif_base + 0x50)
#define MEM_INTF_CTL(chip)			(chip->ops->memif_base + 0x51)
#define MEM_INTF_IMA_CFG(chip)			(chip->ops->memif_base + 0x52)
#define MEM_INTF_IMA_EXP_STS(chip)		(chip->ops->memif_base + 0x55)
#define MEM_INTF_IMA_HW_STS(chip)		(chip->ops->memif_base + 0x56)
#define MEM_INTF_IMA_ERR_STS(chip)		(chip->ops->memif_base + 0x5f)
#define MEM_INTF_IMA_BYTE_EN(chip)		(chip->ops->memif_base + 0x60)
#define MEM_INTF_ADDR_LSB(chip)			(chip->ops->memif_base + 0x61)
#define MEM_INTF_RD_DATA0(chip)			(chip->ops->memif_base + 0x67)
#define MEM_INTF_WR_DATA0(chip)			(chip->ops->memif_base + 0x63)
#define MEM_IF_DMA_STS(chip)			(chip->ops->memif_base + 0x70)
#define MEM_IF_DMA_CTL(chip)			(chip->ops->memif_base + 0x71)

/* SRAM addresses */
#define TEMP_THRESHOLD			0x454
#define BATT_TEMP			0x550
#define BATT_VOLTAGE_CURRENT		0x5cc

#define BATT_TEMP_LSB_MASK		GENMASK(7, 0)
#define BATT_TEMP_MSB_MASK		GENMASK(2, 0)

#define BATT_TEMP_JEITA_COLD		100
#define BATT_TEMP_JEITA_COOL		50
#define BATT_TEMP_JEITA_WARM		400
#define BATT_TEMP_JEITA_HOT		450

#define MEM_INTF_AVAIL			BIT(0)
#define MEM_INTF_CTL_BURST		BIT(7)
#define MEM_INTF_CTL_WR_EN		BIT(6)
#define RIF_MEM_ACCESS_REQ		BIT(7)

#define MEM_IF_TIMEOUT_MS		5000
#define SRAM_ACCESS_RELEASE_DELAY_MS	500

struct qcom_fg_chip;

struct qcom_fg_ops {
	int (*get_capacity)(struct qcom_fg_chip *chip, int *);
	int (*get_temperature)(struct qcom_fg_chip *chip, int *);
	int (*get_current)(struct qcom_fg_chip *chip, int *);
	int (*get_voltage)(struct qcom_fg_chip *chip, int *);
	int (*get_temp_threshold)(struct qcom_fg_chip *chip,
			enum power_supply_property psp, int *);
	int (*set_temp_threshold)(struct qcom_fg_chip *chip,
			enum power_supply_property psp, int);

	short memif_base;
};

struct qcom_fg_chip {
	struct device *dev;
	unsigned int base;
	struct regmap *regmap;
	const struct qcom_fg_ops *ops;
	struct notifier_block nb;

	struct power_supply *batt_psy;
	struct power_supply_battery_info *batt_info;
	struct power_supply *chg_psy;
	int status;
	int voltage_min_design_uv;
	int voltage_max_design_uv;
	int charge_full_design_uah;
	struct delayed_work status_changed_work;
	bool initialized;
	bool notifier_registered;

	struct completion sram_access_granted;
	struct completion sram_access_revoked;
	struct workqueue_struct *sram_wq;
	struct delayed_work sram_release_access_work;
	spinlock_t sram_request_lock;
	spinlock_t sram_rw_lock;
	int sram_requests;
};


int qcom_fg_read(struct qcom_fg_chip *chip, u8 *val, u16 addr, int len);
int qcom_fg_masked_write(struct qcom_fg_chip *chip, u16 addr, u8 mask, u8 val);
bool qcom_fg_sram_check_access(struct qcom_fg_chip *chip);
void qcom_fg_sram_release_access_worker(struct work_struct *work);
int qcom_fg_sram_read(struct qcom_fg_chip *chip,
		u8 *val, u16 addr, int len, int offset);
int qcom_fg_sram_write(struct qcom_fg_chip *chip,
		u8 *val, u16 addr, int len, int offset);
int qcom_fg_clear_ima(struct qcom_fg_chip *chip,
		bool check_hw_sts);
int qcom_fg_get_capacity(struct qcom_fg_chip *chip, int *val);
int qcom_fg_get_temperature(struct qcom_fg_chip *chip, int *val);
int qcom_fg_get_current(struct qcom_fg_chip *chip, int *val);
int qcom_fg_get_voltage(struct qcom_fg_chip *chip, int *val);
int qcom_fg_get_temp_threshold(struct qcom_fg_chip *chip,
				enum power_supply_property psp, int *val);
int qcom_fg_set_temp_threshold(struct qcom_fg_chip *chip,
				enum power_supply_property psp, int val);
int qcom_fg_gen3_get_temperature(struct qcom_fg_chip *chip, int *val);
int qcom_fg_gen3_get_current(struct qcom_fg_chip *chip, int *val);
int qcom_fg_gen3_get_voltage(struct qcom_fg_chip *chip, int *val);
int qcom_fg_gen3_get_temp_threshold(struct qcom_fg_chip *chip,
				enum power_supply_property psp, int *val);
int qcom_fg_gen4_get_temperature(struct qcom_fg_chip *chip, int *val);


#endif

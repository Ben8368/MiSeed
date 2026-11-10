// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020, The Linux Foundation. All rights reserved. */

#include "qcom_fg_internal.h"

/************************
 * BATTERY POWER SUPPLY
 * **********************/

/* Pre-Gen3 fuel gauge. PMI8996 and older */
static const struct qcom_fg_ops ops_fg = {
	.get_capacity = qcom_fg_get_capacity,
	.get_temperature = qcom_fg_get_temperature,
	.get_current = qcom_fg_get_current,
	.get_voltage = qcom_fg_get_voltage,
	.get_temp_threshold = qcom_fg_get_temp_threshold,
	.set_temp_threshold = qcom_fg_set_temp_threshold,
	.memif_base = 0x400,
};

/* Gen3 fuel gauge. PMI8998 and newer */
static const struct qcom_fg_ops ops_fg_gen3 = {
	.get_capacity = qcom_fg_get_capacity,
	.get_temperature = qcom_fg_gen3_get_temperature,
	.get_current = qcom_fg_gen3_get_current,
	.get_voltage = qcom_fg_gen3_get_voltage,
	.get_temp_threshold = qcom_fg_gen3_get_temp_threshold,
	.memif_base = 0x400,
};

/* Gen4 fuel gauge. PM8150B and newer */
static const struct qcom_fg_ops ops_fg_gen4 = {
	.get_capacity = qcom_fg_get_capacity,
	.get_temperature = qcom_fg_gen4_get_temperature,
	.get_current = qcom_fg_gen3_get_current,
	.get_voltage = qcom_fg_gen3_get_voltage,
	.get_temp_threshold = qcom_fg_gen3_get_temp_threshold,
	.memif_base = 0x300,
};

static enum power_supply_property qcom_fg_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TEMP_MIN,
	POWER_SUPPLY_PROP_TEMP_MAX,
	POWER_SUPPLY_PROP_TEMP_ALERT_MIN,
	POWER_SUPPLY_PROP_TEMP_ALERT_MAX,
};

/* The Qualcomm ADC uses discharge-positive current. The standard battery
 * CURRENT_NOW ABI is charge-positive; keep raw decoding internal to the FG.
 */
static int qcom_fg_current_now(struct qcom_fg_chip *chip, int *current_ua)
{
	int raw, ret = chip->ops->get_current(chip, &raw);

	if (ret)
		return ret;
	if (raw == INT_MIN)
		return -ERANGE;
	*current_ua = -raw;
	return 0;
}

static int qcom_fg_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct qcom_fg_chip *chip = power_supply_get_drvdata(psy);
	int temp, ret = 0;

	dev_dbg(chip->dev, "Getting property: %d", psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (!READ_ONCE(chip->initialized))
			return -EAGAIN;
		/* Get status from charger if available */
		if (chip->chg_psy &&
			chip->status != POWER_SUPPLY_STATUS_UNKNOWN) {
			val->intval = chip->status;
			break;
		} else {
			/*
			 * Fall back to capacity and current-based
			 * status checking
			 */
			ret = chip->ops->get_capacity(chip, &temp);
			if (ret) {
				val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
				break;
			}
			if (temp == 100) {
				val->intval = POWER_SUPPLY_STATUS_FULL;
				break;
			}

			ret = chip->ops->get_current(chip, &temp);
			if (ret) {
				val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
				break;
			}
			if (temp < 0)
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			else if (temp > 0)
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			else
				val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		}

		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		if (!READ_ONCE(chip->initialized))
			return -EAGAIN;
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		if (!READ_ONCE(chip->initialized))
			return -EAGAIN;
		ret = chip->ops->get_capacity(chip, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (!READ_ONCE(chip->initialized))
			return -EAGAIN;
		ret = qcom_fg_current_now(chip, &val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (!READ_ONCE(chip->initialized))
			return -EAGAIN;
		ret = chip->ops->get_voltage(chip, &val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		if (!READ_ONCE(chip->initialized))
			return -EAGAIN;
		val->intval = chip->voltage_min_design_uv;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		if (!READ_ONCE(chip->initialized))
			return -EAGAIN;
		val->intval = chip->voltage_max_design_uv;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		if (!READ_ONCE(chip->initialized))
			return -EAGAIN;
		val->intval = chip->charge_full_design_uah;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		if (!READ_ONCE(chip->initialized))
			return -EAGAIN;
		if (chip->ops == &ops_fg_gen4) {
			u8 missing;
			ret = qcom_fg_read(chip, &missing, 0x107, 1);
			if (!ret)
				val->intval = !(missing & (BIT(0) | BIT(1) | BIT(2)));
		} else {
			val->intval = 1;
		}
		break;
	case POWER_SUPPLY_PROP_TEMP:
		if (!READ_ONCE(chip->initialized))
			return -EAGAIN;
		ret = chip->ops->get_temperature(chip, &val->intval);
		break;
	case POWER_SUPPLY_PROP_TEMP_MIN:
	case POWER_SUPPLY_PROP_TEMP_MAX:
	case POWER_SUPPLY_PROP_TEMP_ALERT_MIN:
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		if (!READ_ONCE(chip->initialized))
			return -EAGAIN;
		ret = chip->ops->get_temp_threshold(chip, psp, &val->intval);
		break;
	default:
		dev_err(chip->dev, "invalid property: %d\n", psp);
		return -EINVAL;
	}

	return ret;
}

/* Charger changes propagate through power_supply's supplier graph, without
 * making the gauge depend on a charger that itself requires gauge telemetry.
 */
static void qcom_fg_external_power_changed(struct power_supply *psy)
{
	struct qcom_fg_chip *chip = power_supply_get_drvdata(psy);

	if (READ_ONCE(chip->initialized))
		power_supply_changed(psy);
}

static const struct power_supply_desc batt_psy_desc = {
	.name = "qcom-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = qcom_fg_props,
	.num_properties = ARRAY_SIZE(qcom_fg_props),
	.get_property = qcom_fg_get_property,
	.external_power_changed = qcom_fg_external_power_changed,
};

static irqreturn_t qcom_fg_handle_soc_delta(int irq, void *data)
{
	struct qcom_fg_chip *chip = data;

	/* Signal change in state of charge */
	power_supply_changed(chip->batt_psy);
	dev_dbg(chip->dev, "SOC changed");

	return IRQ_HANDLED;
}

static irqreturn_t qcom_fg_handle_mem_avail(int irq, void *data)
{
	struct qcom_fg_chip *chip = data;

	if (qcom_fg_sram_check_access(chip)) {
		complete_all(&chip->sram_access_granted);
		dev_dbg(chip->dev, "SRAM access granted");
	} else {
		complete_all(&chip->sram_access_revoked);
		dev_dbg(chip->dev, "SRAM access revoked");
	}

	return IRQ_HANDLED;
}

static void qcom_fg_status_changed_worker(struct work_struct *work)
{
	struct qcom_fg_chip *chip = container_of(work, struct qcom_fg_chip,
						status_changed_work.work);

	power_supply_changed(chip->batt_psy);
}

static int qcom_fg_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	struct qcom_fg_chip *chip = container_of(nb, struct qcom_fg_chip, nb);
	struct power_supply *psy = v;
	union power_supply_propval propval;
	int ret;

	if (!READ_ONCE(chip->initialized))
		return NOTIFY_DONE;

	if (psy == chip->chg_psy) {
		ret = power_supply_get_property(psy,
				POWER_SUPPLY_PROP_STATUS, &propval);
		if (ret) {
			chip->status = POWER_SUPPLY_STATUS_UNKNOWN;
		} else {
			chip->status = propval.intval;
		}

		power_supply_changed(chip->batt_psy);

		if (chip->status == POWER_SUPPLY_STATUS_UNKNOWN) {
			/*
			 * REVISIT: Find better solution or remove current-based
			 * status checking once checking is properly implemented
			 * in charger drivers

			 * Sometimes it take a while for current to stabilize,
			 * so signal property change again later to make sure
			 * current-based status is properly detected.
			 */
			cancel_delayed_work_sync(&chip->status_changed_work);
			schedule_delayed_work(&chip->status_changed_work,
						msecs_to_jiffies(1000));
		}
	}

	return NOTIFY_OK;
}

static void qcom_fg_cleanup_sram(void *data)
{
	struct qcom_fg_chip *chip = data;
	int ret;

	if (!chip->sram_wq)
		return;

	cancel_delayed_work_sync(&chip->sram_release_access_work);
	spin_lock(&chip->sram_request_lock);
	if (!chip->sram_requests) {
		ret = qcom_fg_masked_write(chip, MEM_INTF_CFG(chip),
				RIF_MEM_ACCESS_REQ, 0);
		if (ret)
			dev_err(chip->dev, "Failed to clear SRAM request during cleanup: %d\n",
				ret);
	}
	spin_unlock(&chip->sram_request_lock);
	destroy_workqueue(chip->sram_wq);
	chip->sram_wq = NULL;
}

static void qcom_fg_release_battery_info(void *data)
{
	struct qcom_fg_chip *chip = data;

	if (chip->batt_info) {
		power_supply_put_battery_info(chip->batt_psy, chip->batt_info);
		chip->batt_info = NULL;
	}
}

static int qcom_fg_probe(struct platform_device *pdev)
{
	struct power_supply_config supply_config = {};
	struct qcom_fg_chip *chip;
	const __be32 *prop_addr;
	int irq;
	u8 dma_status;
	bool error_present;
	int ret;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	chip->ops = of_device_get_match_data(&pdev->dev);

	chip->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chip->regmap) {
		dev_err(chip->dev, "Failed to locate the regmap\n");
		return -ENODEV;
	}

	/* Get base address */
	prop_addr = of_get_address(pdev->dev.of_node, 0, NULL, NULL);
	if (!prop_addr) {
		dev_err(chip->dev, "Failed to read SOC base address from dt\n");
		return -EINVAL;
	}
	chip->base = be32_to_cpu(*prop_addr);
	spin_lock_init(&chip->sram_request_lock);
	spin_lock_init(&chip->sram_rw_lock);

	/* Gen4 getters below use peripheral readback only. Do not touch IMA,
	 * DMA, profile SRAM or JEITA thresholds just to expose telemetry. */
	if (!of_device_is_compatible(pdev->dev.of_node, "qcom,pm8150b-fg")) {
	/*
	 * Change the FG_MEM_INT interrupt to track IACS_READY
	 * condition instead of end-of-transaction. This makes sure
	 * that the next transaction starts only after the hw is ready.
	 * IACS_INTR_SRC_SLCT is BIT(3)
	 */
	ret = qcom_fg_masked_write(chip,
		MEM_INTF_IMA_CFG(chip), BIT(3), BIT(3));
	if (ret) {
		dev_err(chip->dev,
			"Failed to configure interrupt source: %d\n", ret);
		return ret;
	}

	ret = qcom_fg_clear_ima(chip, true);
	if (ret && ret != -EAGAIN) {
		dev_err(chip->dev, "Failed to clear IMA exception: %d\n", ret);
		return ret;
	}

	/* Check and clear DMA errors */
	ret = qcom_fg_read(chip, &dma_status, MEM_IF_DMA_STS(chip), 1);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read dma_status: %d\n", ret);
		return ret;
	}

	error_present = dma_status & (BIT(1) | BIT(2));
	ret = qcom_fg_masked_write(chip, MEM_IF_DMA_CTL(chip), BIT(0),
			error_present ? BIT(0) : 0);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to write dma_ctl: %d\n", ret);
		return ret;
	}

	}

	supply_config.drv_data = chip;
	supply_config.fwnode = pdev->dev.fwnode;

	ret = devm_add_action_or_reset(chip->dev, qcom_fg_cleanup_sram, chip);
	if (ret)
		return ret;

	chip->batt_psy = devm_power_supply_register(chip->dev,
			&batt_psy_desc, &supply_config);
	if (IS_ERR(chip->batt_psy)) {
		if (PTR_ERR(chip->batt_psy) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to register battery\n");
		return PTR_ERR(chip->batt_psy);
	}

	platform_set_drvdata(pdev, chip);

	ret = power_supply_get_battery_info(chip->batt_psy, &chip->batt_info);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get battery info: %d\n", ret);
		return ret;
	}
	chip->voltage_min_design_uv = chip->batt_info->voltage_min_design_uv;
	chip->voltage_max_design_uv = chip->batt_info->voltage_max_design_uv;
	chip->charge_full_design_uah = chip->batt_info->charge_full_design_uah;
	ret = devm_add_action_or_reset(chip->dev, qcom_fg_release_battery_info, chip);
	if (ret)
		return ret;

	/* Initialize SRAM */
	if (of_device_is_compatible(pdev->dev.of_node, "qcom,pmi8994-fg")) {
		irq = of_irq_get_byname(pdev->dev.of_node, "mem-avail");
		if (irq < 0) {
			dev_err(&pdev->dev, "Failed to get irq mem-avail byname: %d\n",
				irq);
			return irq;
		}

		init_completion(&chip->sram_access_granted);
		init_completion(&chip->sram_access_revoked);

		chip->sram_wq = create_singlethread_workqueue("qcom_fg");
		if (!chip->sram_wq)
			return -ENOMEM;
		INIT_DELAYED_WORK(&chip->sram_release_access_work,
			qcom_fg_sram_release_access_worker);

		ret = devm_request_threaded_irq(chip->dev, irq, NULL,
						qcom_fg_handle_mem_avail,
						IRQF_ONESHOT, "mem-avail", chip);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to request mem-avail IRQ: %d\n", ret);
			return ret;
		}

		chip->sram_requests = 0;
	}

	/* Set default temperature thresholds */
	if (chip->ops->set_temp_threshold) {
		ret = chip->ops->set_temp_threshold(chip,
						POWER_SUPPLY_PROP_TEMP_MIN,
						BATT_TEMP_JEITA_COLD);
		if (ret) {
			dev_err(chip->dev,
				"Failed to set cold threshold: %d\n", ret);
			return ret;
		}

		ret = chip->ops->set_temp_threshold(chip,
						POWER_SUPPLY_PROP_TEMP_MAX,
						BATT_TEMP_JEITA_WARM);
		if (ret) {
			dev_err(chip->dev,
				"Failed to set warm threshold: %d\n", ret);
			return ret;
		}

		ret = chip->ops->set_temp_threshold(chip,
						POWER_SUPPLY_PROP_TEMP_ALERT_MIN,
						BATT_TEMP_JEITA_COOL);
		if (ret) {
			dev_err(chip->dev,
				"Failed to set cool threshold: %d\n", ret);
			return ret;
		}

		ret = chip->ops->set_temp_threshold(chip,
						POWER_SUPPLY_PROP_TEMP_ALERT_MAX,
						BATT_TEMP_JEITA_HOT);
		if (ret) {
			dev_err(chip->dev,
				"Failed to set hot threshold: %d\n", ret);
			return ret;
		}
	}

	/* Get soc-delta IRQ */
	irq = of_irq_get_byname(pdev->dev.of_node, "soc-delta");
	if (irq < 0) {
		dev_err(&pdev->dev, "Failed to get irq soc-delta byname: %d\n",
			irq);
		return irq;
	}

	ret = devm_request_threaded_irq(chip->dev, irq, NULL,
					qcom_fg_handle_soc_delta,
					IRQF_ONESHOT, "soc-delta", chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request soc-delta IRQ: %d\n", ret);
		return ret;
	}

/* Optional: Get charger power supply for status checking */
chip->chg_psy = devm_power_supply_get_by_reference(chip->dev,
						    "power-supplies");
if (IS_ERR(chip->chg_psy)) {
	if (PTR_ERR(chip->chg_psy) == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	dev_warn(chip->dev, "Failed to get charger supply: %ld\n",
		 PTR_ERR(chip->chg_psy));
	chip->chg_psy = NULL;
}

	if (chip->chg_psy) {
		INIT_DELAYED_WORK(&chip->status_changed_work,
			qcom_fg_status_changed_worker);

		chip->nb.notifier_call = qcom_fg_notifier_call;
		ret = power_supply_reg_notifier(&chip->nb);
		if (ret) {
			dev_err(chip->dev,
				"Failed to register notifier: %d\n", ret);
			return ret;
		}
		chip->notifier_registered = true;
	}

	WRITE_ONCE(chip->initialized, true);

	return 0;
}

static void qcom_fg_remove(struct platform_device *pdev)
{
	struct qcom_fg_chip *chip = platform_get_drvdata(pdev);

	WRITE_ONCE(chip->initialized, false);
	if (chip->notifier_registered) {
		power_supply_unreg_notifier(&chip->nb);
		chip->notifier_registered = false;
	}
	if (chip->chg_psy)
		cancel_delayed_work_sync(&chip->status_changed_work);
}

static const struct of_device_id fg_match_id_table[] = {
	{ .compatible = "qcom,pmi8994-fg", .data = &ops_fg },
	{ .compatible = "qcom,pmi8998-fg", .data = &ops_fg_gen3 },
	{ .compatible = "qcom,pm8150b-fg", .data = &ops_fg_gen4 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fg_match_id_table);

static struct platform_driver qcom_fg_driver = {
	.probe = qcom_fg_probe,
	.remove = qcom_fg_remove,
	.driver = {
		.name = "qcom-fg",
		.of_match_table = fg_match_id_table,
	},
};

module_platform_driver(qcom_fg_driver);

MODULE_AUTHOR("Caleb Connolly <caleb@connolly.tech>");
MODULE_AUTHOR("Joel Selvaraj <jo@jsfamily.in>");
MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("Qualcomm PMIC Fuel Gauge Driver");
MODULE_LICENSE("GPL v2");

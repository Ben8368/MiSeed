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

#include "qcom_pm8150b_internal.h"

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
	static char *supplied_to[] = { "qcom-battery" };
	struct power_supply_config supply_config = {
		.supplied_to = supplied_to, .num_supplicants = ARRAY_SIZE(supplied_to),
	};
	struct power_supply_desc *desc;
	struct smb5_chip *chip;
	int rc, irq;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	chip->name = pdev->name;
	mutex_init(&chip->lock);
	/* LOS automatic charger policy: the worker enables only after telemetry,
	 * charger limits, health and temperature have been checked.
	 */
	chip->inhibit = false;
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
 chip->bms = devm_power_supply_get_by_reference(chip->dev, "qcom,fuel-gauge");
 if (IS_ERR(chip->bms)) return PTR_ERR(chip->bms);
 if (!chip->bms) return -EPROBE_DEFER;
 if (!device_link_add(chip->dev, chip->bms->dev.parent, DL_FLAG_AUTOREMOVE_CONSUMER))
  return -ENOMEM;
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
	rc = smb5_register_thermal(chip);
	if (rc)
		return rc;
#ifdef CONFIG_MISEED_CHARGE_DIAGNOSTICS
 rc = devm_device_add_group(&chip->chg_psy->dev, &smb5_group);
 if (rc) return rc;
#endif


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

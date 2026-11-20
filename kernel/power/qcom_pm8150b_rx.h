// SPDX-License-Identifier: GPL-2.0-only
/* A shared USBIN presence bit cannot identify Umi's wired source. */
#ifndef QCOM_PM8150B_RX_H
#define QCOM_PM8150B_RX_H

#define SMB5_RX_REFERENCE "qcom,wireless-rx"

static int smb5_rx_changed(struct notifier_block *notifier,
			   unsigned long event, void *data)
{
	struct smb5_chip *chip = container_of(notifier, struct smb5_chip, rx_notifier);

	if (event != PSY_EVENT_PROP_CHANGED || data != chip->wireless_rx)
		return NOTIFY_DONE;
	mutex_lock(&chip->lock);
	if (!chip->stopping)
		mod_delayed_work(system_wq, &chip->status_change_work, 0);
	mutex_unlock(&chip->lock);
	return NOTIFY_OK;
}

static void smb5_rx_unregister(void *data)
{
	struct smb5_chip *chip = data;

	power_supply_unreg_notifier(&chip->rx_notifier);
}

static int smb5_rx_attach(struct smb5_chip *chip)
{
	struct device *dev = chip->dev;

	if (!device_property_present(dev, SMB5_RX_REFERENCE))
		return 0;
	chip->wireless_rx = devm_power_supply_get_by_reference(dev, SMB5_RX_REFERENCE);
	if (IS_ERR(chip->wireless_rx))
		return PTR_ERR(chip->wireless_rx);
	if (!chip->wireless_rx)
		return -EPROBE_DEFER;
	if (!device_link_add(dev, chip->wireless_rx->dev.parent, DL_FLAG_AUTOREMOVE_CONSUMER))
		return -ENOMEM;
	return 0;
}

static int smb5_rx_subscribe(struct smb5_chip *chip)
{
	int ret;

	if (!chip->wireless_rx)
		return 0;
	chip->rx_notifier.notifier_call = smb5_rx_changed;
	ret = power_supply_reg_notifier(&chip->rx_notifier);
	if (ret)
		return ret;
	return devm_add_action_or_reset(chip->dev, smb5_rx_unregister, chip);
}

#endif

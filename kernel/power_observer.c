// SPDX-License-Identifier: GPL-2.0-only
/*
 * MiSeed Umi diagnostic observer. No PMIC writes, IRQ acknowledgements,
 * charger initialization, profile loading or power_supply controls.
 * SMB5 status layout: Qualcomm/The Linux Foundation smb5-reg.h and
 * qcom_smbx.c, referenced in the charging-log evidence record.
 */
#include <linux/bits.h>
#include <linux/devm-helpers.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>

#define OBS_CHGR_STATUS1 0x1006
#define OBS_CHGR_STATUS2 0x1007
#define OBS_CHGR_ENABLE  0x1042
#define OBS_USB_RT      0x1310
#define OBS_USB_PLUGIN  BIT(4)

struct power_observer {
	struct device *dev;
	struct regmap *regmap;
	struct delayed_work work;
};

static void power_observer_sample(struct power_observer *obs)
{
	static const char * const phases[] = {
		"inhibit", "trickle", "precharge", "fullon",
		"taper", "terminate", "pause", "disabled",
	};
	static const unsigned int addresses[] = {
		OBS_USB_RT, OBS_CHGR_STATUS1, OBS_CHGR_STATUS2, OBS_CHGR_ENABLE,
	};
	unsigned int values[ARRAY_SIZE(addresses)];
	int rc, i;

	for (i = 0; i < ARRAY_SIZE(addresses); i++) {
		rc = regmap_read(obs->regmap, addresses[i], &values[i]);
		if (rc < 0) {
			dev_info(obs->dev, "miseed-power read_error reg=0x%04x rc=%d\n",
				 addresses[i], rc);
			return;
		}
	}

	/* Hardware indications only; phase/enable alone do not prove net charge. */
	dev_info(obs->dev,
		 "miseed-power usb_present=%u hw_phase=%s chg_enable=%u usb_rt=0x%02x chgr1=0x%02x chgr2=0x%02x control=read-only\n",
		 !!(values[0] & OBS_USB_PLUGIN), phases[values[1] & GENMASK(2, 0)],
		 !!(values[3] & BIT(0)), values[0], values[1], values[2]);
}

static void power_observer_work(struct work_struct *work)
{
	struct power_observer *obs = container_of(work, struct power_observer,
						work.work);

	power_observer_sample(obs);
	schedule_delayed_work(&obs->work, msecs_to_jiffies(1000));
}

static int power_observer_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct power_observer *obs;
	int rc;

	if (!dev->parent || !of_device_is_compatible(dev->parent->of_node,
						    "qcom,pm8150b"))
		return -ENODEV;
	obs = devm_kzalloc(dev, sizeof(*obs), GFP_KERNEL);
	if (!obs)
		return -ENOMEM;
	obs->dev = dev;
	obs->regmap = dev_get_regmap(dev->parent, NULL);
	if (!obs->regmap)
		return -EPROBE_DEFER;
	rc = devm_delayed_work_autocancel(dev, &obs->work, power_observer_work);
	if (rc)
		return rc;
	dev_info(dev, "miseed-power observer started; no charging control; temperature/SOC unavailable without calibrated FG\n");
	schedule_delayed_work(&obs->work, 0);
	return 0;
}

static const struct of_device_id power_observer_match[] = {
	{ .compatible = "miseed,umi-power-observer" },
	{ }
};
MODULE_DEVICE_TABLE(of, power_observer_match);

static struct platform_driver power_observer_driver = {
	.probe = power_observer_probe,
	.driver = {
		.name = "miseed-power-observer",
		.of_match_table = power_observer_match,
	},
};
module_platform_driver(power_observer_driver);
MODULE_DESCRIPTION("MiSeed Umi read-only charger status observer");
MODULE_LICENSE("GPL");

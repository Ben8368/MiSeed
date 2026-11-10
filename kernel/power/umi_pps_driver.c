// SPDX-License-Identifier: GPL-2.0-only
/*
 * Umi BQ25970 + mainline TCPM PPS integration.
 * Register encodings / 2:1 tuning adapted from LineageOS
 * android_kernel_xiaomi_sm8250 71b13e62f057a649b77fe4062feb73ee72ad609c.
 * BQ source: Copyright (C) 2017 Texas Instruments; GPL version 2.
 * Policy MODULE_AUTHOR: Fei Jiang <jiangfei1@xiaomi.com>.
 * Policy header credits: a0220433 (Mar 27, 2017). See NOTICE.md.
 * No vendor power_supply ABI, pmic-voter, charger authentication or QC writes.
 */
#include "umi_pps_internal.h"

static irqreturn_t umi_pps_irq(int irq, void *data)
{
	struct umi_pps *c = data;

	/* Fault flags are collected by the same serialized reader as polling. */
	mutex_lock(&c->lock);
	if (c->ready && !c->removing)
		mod_delayed_work(system_wq, &c->work, 0);
	mutex_unlock(&c->lock);
	return IRQ_HANDLED;
}

/* Standard wired supply ABI. Reading it never consumes latched fault flags. */
static void umi_pps_cleanup(void *data)
{
	struct umi_pps *c = data;

	mutex_lock(&c->lock);
	c->removing = true;
	c->ready = false;
	mutex_unlock(&c->lock);
	cancel_delayed_work_sync(&c->work);
	mutex_lock(&c->lock);
	umi_pps_stop(c, PPS_OFF, 0);
	mutex_unlock(&c->lock);
}

static int umi_pps_suspend(struct device *dev)
{
	struct umi_pps *c = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&c->lock);
	c->resume_pps = !c->manual_stop && (c->state == PPS_WAIT_PLUG ||
			c->state == PPS_SETTLE || c->state == PPS_RUN);
	ret = umi_pps_stop(c, c->resume_pps ? PPS_OFF : c->state, c->last_error);
	c->suspended = !ret;
	mutex_unlock(&c->lock);
	/* Refuse suspend if power could not be made safe; retry while awake. */
	if (ret)
		mod_delayed_work(system_wq, &c->work, 0);
	return ret;
}
static int umi_pps_resume(struct device *dev)
{
	struct umi_pps *c = dev_get_drvdata(dev);

	mutex_lock(&c->lock);
	c->suspended = false;
	if (c->resume_pps && !c->manual_stop && !c->stop_pending && c->state == PPS_OFF)
		c->state = PPS_WAIT_PLUG;
	c->resume_pps = false;
	mod_delayed_work(system_wq, &c->work, 0);
	mutex_unlock(&c->lock);
	return 0;
}
static DEFINE_SIMPLE_DEV_PM_OPS(umi_pps_pm, umi_pps_suspend, umi_pps_resume);

static const struct regmap_config umi_pps_regmap = {
	.reg_bits = 8, .val_bits = 8, .max_register = 0x2e,
	.cache_type = REGCACHE_NONE,
};

static int umi_pps_probe(struct i2c_client *client)
{
	static const char * const thermal_names[] = {
		"cam_therm1", "conn_therm", "quiet_therm", "wireless_therm", "xo_therm"
	};
	struct device *dev = &client->dev;
	static char *supplied_to[] = { "qcom-battery" };
	struct power_supply_config config = {
		.fwnode = dev_fwnode(dev), .supplied_to = supplied_to,
		.num_supplicants = ARRAY_SIZE(supplied_to),
	};
	struct umi_pps *c;
	unsigned int id, mode;
	int ret, i;

	c = devm_kzalloc(dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;
	c->dev = dev;
	mutex_init(&c->lock);
	INIT_DELAYED_WORK(&c->work, umi_pps_work);
	i2c_set_clientdata(client, c);
	c->map = devm_regmap_init_i2c(client, &umi_pps_regmap);
	if (IS_ERR(c->map))
		return PTR_ERR(c->map);
	ret = regmap_read(c->map, BQ_ID, &id);
	if (ret)
		return ret;
	if (id != 0x10)
		return dev_err_probe(dev, -ENODEV, "unqualified pump ID 0x%x (no clone fallback)\n", id);
	ret = regmap_read(c->map, BQ_CONTROL, &mode);
	if (ret)
		return ret;
	if (mode & 0x60)
		return dev_err_probe(dev, -EINVAL, "only standalone BQ25970 is qualified by this port\n");
	ret = umi_pps_disable(c);
	if (ret)
		return ret;
	ret = umi_pps_parse(c);
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(thermal_names); i++) {
		enum iio_chan_type type;

		c->thermal_channels[i] = devm_iio_channel_get(dev, thermal_names[i]);
		if (IS_ERR(c->thermal_channels[i]))
			return dev_err_probe(dev, PTR_ERR(c->thermal_channels[i]),
					     "missing thermal channel %s\n", thermal_names[i]);
		ret = iio_get_channel_type(c->thermal_channels[i], &type);
		if (ret || type != IIO_TEMP)
			return dev_err_probe(dev, ret ? ret : -EINVAL,
					     "thermal channel must report temperature\n");
	}
	c->sw = devm_power_supply_get_by_reference(dev, "xiaomi,switch-charger");
	if (IS_ERR(c->sw))
		return PTR_ERR(c->sw);
	if (!c->sw)
		return -EPROBE_DEFER;
	c->bms = devm_power_supply_get_by_reference(dev, "xiaomi,fuel-gauge");
	if (IS_ERR(c->bms))
		return PTR_ERR(c->bms);
	if (!c->bms)
		return -EPROBE_DEFER;
	c->pd = devm_power_supply_get_by_reference(dev, "power-supplies");
	if (IS_ERR(c->pd))
		return PTR_ERR(c->pd);
	if (!c->pd)
		return -EPROBE_DEFER;
	/* Unbind the consumer before any supply driver releases its private data. */
	if (!device_link_add(dev, c->sw->dev.parent, DL_FLAG_AUTOREMOVE_CONSUMER) ||
	    !device_link_add(dev, c->bms->dev.parent, DL_FLAG_AUTOREMOVE_CONSUMER) ||
	    !device_link_add(dev, c->pd->dev.parent, DL_FLAG_AUTOREMOVE_CONSUMER))
		return -ENOMEM;
	if (client->irq <= 0)
		return dev_err_probe(dev, -EINVAL, "qualified fault IRQ is required\n");
	ret = devm_request_threaded_irq(dev, client->irq, NULL, umi_pps_irq,
			IRQF_ONESHOT, dev_name(dev), c);
	if (ret)
		return ret;
	config.drv_data = c;
	c->psy = devm_power_supply_register(dev, &umi_pps_desc, &config);
	if (IS_ERR(c->psy))
		return PTR_ERR(c->psy);
	c->cooling = devm_thermal_of_cooling_device_register(dev, dev->of_node,
			"umi-wired-pps", c, &umi_pps_cooling_ops);
	if (IS_ERR(c->cooling))
		return PTR_ERR(c->cooling);
	c->tzd = devm_thermal_of_zone_register(dev, 0, c, &umi_pps_thermal_ops);
	if (IS_ERR(c->tzd))
		return PTR_ERR(c->tzd);
	/* Registered last: drain the worker before devres frees any interface. */
	ret = devm_add_action_or_reset(dev, umi_pps_cleanup, c);
	if (ret)
		return ret;
	mutex_lock(&c->lock);
	c->state = PPS_WAIT_PLUG;
	c->ready = true;
	mutex_unlock(&c->lock);
	power_supply_changed(c->psy);
	dev_info(dev, "wired PPS power_supply/thermal ready; waiting for USB-PD attach\n");
	mod_delayed_work(system_wq, &c->work, 0);
	return ret;
}

static void umi_pps_shutdown(struct i2c_client *client)
{
	umi_pps_cleanup(i2c_get_clientdata(client));
}

static const struct of_device_id umi_pps_match[] = {
	{ .compatible = "xiaomi,umi-bq25970" }, {}
};
MODULE_DEVICE_TABLE(of, umi_pps_match);
static struct i2c_driver umi_pps_driver = {
	.driver = { .name = "umi-bq25970-pps", .of_match_table = umi_pps_match,
		.pm = pm_sleep_ptr(&umi_pps_pm) },
	.probe = umi_pps_probe, .shutdown = umi_pps_shutdown,
};
module_i2c_driver(umi_pps_driver);
MODULE_AUTHOR("Texas Instruments; MiSeed contributors");
MODULE_DESCRIPTION("Umi guarded BQ25970/mainline TCPM PPS charging");
MODULE_LICENSE("GPL");

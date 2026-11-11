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
#include "bq25970_device.h"

static void umi_pps_event(void *data)
{
	struct umi_pps *c = data;

	/* Fault flags are collected by the same serialized reader as polling. */
	mutex_lock(&c->lock);
	if (c->ready && !c->removing)
		mod_delayed_work(system_wq, &c->work, 0);
	mutex_unlock(&c->lock);

}

/* Standard wired supply ABI. Reading it never consumes latched fault flags. */
static void umi_pps_cleanup(void *data)
{
	struct umi_pps *c = data;
	int ret;

	mutex_lock(&c->lock);
	c->removing = true;
	c->ready = false;
	mutex_unlock(&c->lock);
	cancel_delayed_work_sync(&c->work);
	/* remove/shutdown cannot return an error to veto teardown. Keep policy,
	 * suppliers, IRQ and regmap alive until the full stop transaction succeeds.
	 * Persistent I/O failure deliberately blocks teardown; never free the last
	 * owner of a running pump or a pending PD/SMB5 recovery.
	 */
	do {
		mutex_lock(&c->lock);
		ret = umi_pps_stop(c, PPS_OFF, 0);
		mutex_unlock(&c->lock);
		if (ret) {
			dev_err_ratelimited(c->dev, "wired PPS teardown waiting for safe stop: %d\n", ret);
			msleep(UMI_PPS_PERIOD_MS);
		}
	} while (ret);
	/* No policy lock while waiting for an in-flight IRQ callback. */
	bq25970_unsubscribe(c->chip);
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


static int umi_pps_probe(struct auxiliary_device *aux,
			 const struct auxiliary_device_id *id)
{
	static const char * const thermal_names[] = {
		"cam_therm1", "conn_therm", "quiet_therm", "wireless_therm", "xo_therm"
	};
	struct device *dev = &aux->dev;
	static char *supplied_to[] = { "qcom-battery" };
	struct power_supply_config config = {
		.fwnode = dev_fwnode(dev), .supplied_to = supplied_to,
		.num_supplicants = ARRAY_SIZE(supplied_to),
	};
	struct umi_pps *c;
	int ret, i;

	c = devm_kzalloc(dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;
	c->dev = dev;
	mutex_init(&c->lock);
	INIT_DELAYED_WORK(&c->work, umi_pps_work);
	dev_set_drvdata(dev, c);
	c->chip = to_bq25970_device(aux);
	c->map = c->chip->map;
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
	ret = bq25970_subscribe(c->chip, umi_pps_event, c);
	if (ret)
		return ret;
	/* Detach callbacks and drain work before interfaces or state are freed. */
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

static void umi_pps_shutdown(struct auxiliary_device *aux)
{
	umi_pps_cleanup(dev_get_drvdata(&aux->dev));
}
static const struct auxiliary_device_id umi_pps_match[] = {
	{ .name = "bq25970_core.charger" }, { }
};
MODULE_DEVICE_TABLE(auxiliary, umi_pps_match);
static struct auxiliary_driver umi_pps_driver = {
	.name = "umi-pps-policy", .id_table = umi_pps_match,
	.probe = umi_pps_probe, .shutdown = umi_pps_shutdown,
	.driver = { .pm = pm_sleep_ptr(&umi_pps_pm) },
};
module_auxiliary_driver(umi_pps_driver);
MODULE_AUTHOR("Texas Instruments; MiSeed contributors");
MODULE_DESCRIPTION("Umi wired PPS board policy with SMB5 safety ownership");
MODULE_LICENSE("GPL");

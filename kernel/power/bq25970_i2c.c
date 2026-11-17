// SPDX-License-Identifier: GPL-2.0-only
/* BQ25970 I2C lifecycle. No board policy, auxiliary device, PD or PMIC. */
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include "bq25970_internal.h"

int bq25970_subscribe(struct bq25970_device *bq, void (*event)(void *), void *data)
{
	int ret = 0;

	if (!event)
		return -EINVAL;
	mutex_lock(&bq->event_lock);
	mutex_lock(&bq->lock);
	if (bq->owner)
		ret = -EBUSY;
	else {
		bq->event_data = data;
		bq->event = event;
		bq->owner = true;
	}
	mutex_unlock(&bq->lock);
	mutex_unlock(&bq->event_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(bq25970_subscribe);

void bq25970_unsubscribe(struct bq25970_device *bq)
{
	/* The caller first completes stop/recovery without holding its own lock. */
	mutex_lock(&bq->event_lock);
	mutex_lock(&bq->lock);
	bq->event = NULL;
	bq->event_data = NULL;
	bq->owner = false;
	mutex_unlock(&bq->lock);
	mutex_unlock(&bq->event_lock);
}
EXPORT_SYMBOL_GPL(bq25970_unsubscribe);

static irqreturn_t bq25970_irq(int irq, void *data)
{
	struct bq25970_device *bq = data;
	struct bq25970_sample sample;
	int ret;

	/* Never invoke a policy callback while holding the register lock. */
	mutex_lock(&bq->event_lock);
	if (bq->event) {
		bq->event(bq->event_data);
	} else {
		ret = bq25970_stop(bq);
		if (!ret)
			ret = bq25970_read_sample(bq, &sample);
		if (ret)
			dev_err_ratelimited(bq->dev, "unowned pump IRQ failed: %d\n", ret);
	}
	mutex_unlock(&bq->event_lock);
	return IRQ_HANDLED;
}
static const struct regmap_config bq25970_regmap = {
	.reg_bits = 8, .val_bits = 8, .max_register = 0x2e,
	.cache_type = REGCACHE_NONE,
};
void bq25970_shutdown_device(struct bq25970_device *bq)
{
	int ret = bq25970_stop(bq);

	if (ret)
		dev_err(bq->dev, "pump shutdown failed: %d\n", ret);
}
EXPORT_SYMBOL_GPL(bq25970_shutdown_device);
static void bq25970_remove(void *data)
{
	struct bq25970_device *bq = data;

	/* LIFO: IRQ and power_supply have already been released. The board
	 * consumer was drained first, so no remaining path can publish events.
	 */
	bq->psy = NULL;
	bq25970_shutdown_device(bq);
}
struct bq25970_device *bq25970_register(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct bq25970_device *bq;
	struct bq25970_sample sample;
	int ret;

	bq = devm_kzalloc(dev, sizeof(*bq), GFP_KERNEL);
	if (!bq)
		return ERR_PTR(-ENOMEM);
	bq->dev = dev;
	mutex_init(&bq->lock);
	mutex_init(&bq->event_lock);
	bq->map = devm_regmap_init_i2c(client, &bq25970_regmap);
	if (IS_ERR(bq->map))
		return ERR_CAST(bq->map);
	ret = bq25970_check_identity(bq->map);
	if (!ret)
		ret = bq25970_stop(bq);
	if (ret)
		return ERR_PTR(dev_err_probe(dev, ret, "identity/initial stop failed\n"));
	ret = devm_add_action_or_reset(dev, bq25970_remove, bq);
	if (ret)
		return ERR_PTR(ret);
	ret = bq25970_read_sample(bq, &sample);
	if (ret)
		return ERR_PTR(ret);
	ret = bq25970_register_supply(bq);
	if (ret)
		return ERR_PTR(ret);
	if (client->irq > 0) {
		ret = devm_request_threaded_irq(dev, client->irq, NULL, bq25970_irq,
				IRQF_ONESHOT, dev_name(dev), bq);
		if (ret)
			return ERR_PTR(ret);
	}
	return bq;
}
EXPORT_SYMBOL_GPL(bq25970_register);
static int bq25970_probe(struct i2c_client *client)
{
	struct bq25970_device *bq = bq25970_register(client);

	if (IS_ERR(bq))
		return PTR_ERR(bq);
	i2c_set_clientdata(client, bq);
	return 0;
}
static void bq25970_shutdown(struct i2c_client *client)
{
	bq25970_shutdown_device(i2c_get_clientdata(client));
}
static const struct of_device_id bq25970_match[] = {
	{ .compatible = "ti,bq25970" }, { }
};
MODULE_DEVICE_TABLE(of, bq25970_match);
static struct i2c_driver bq25970_driver = {
	.probe = bq25970_probe, .shutdown = bq25970_shutdown,
	.driver = { .name = "bq25970", .of_match_table = bq25970_match },
};
module_i2c_driver(bq25970_driver);
MODULE_DESCRIPTION("BQ25970 independent hardware driver and telemetry");
MODULE_AUTHOR("Texas Instruments; MiSeed contributors");
MODULE_LICENSE("GPL");

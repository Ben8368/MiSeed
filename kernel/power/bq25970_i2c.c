// SPDX-License-Identifier: GPL-2.0-only
/* BQ25970 standalone silicon transport/lifecycle. No Umi/TCPM/SMB5 policy.
 * Register provenance and TI attribution are retained in bq25970.c/NOTICE.md.
 */
#include <linux/i2c.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include "bq25970.h"
#include "bq25970_device.h"

static DEFINE_IDA(bq25970_ids);
int bq25970_subscribe(struct bq25970_device *bq, void (*event)(void *), void *data)
{
	int ret = 0;

	if (!event)
		return -EINVAL;
	mutex_lock(&bq->event_lock);
	if (bq->event)
		ret = -EBUSY;
	else {
		bq->event_data = data;
		bq->event = event;
	}
	mutex_unlock(&bq->event_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(bq25970_subscribe);
void bq25970_unsubscribe(struct bq25970_device *bq)
{
	/* Wait for any callback before its consumer can free its state. */
	mutex_lock(&bq->event_lock);
	bq->event = NULL;
	bq->event_data = NULL;
	mutex_unlock(&bq->event_lock);
}
EXPORT_SYMBOL_GPL(bq25970_unsubscribe);
static irqreturn_t bq25970_irq(int irq, void *data)
{
	struct bq25970_device *bq = data;
	int ret = 0;

	mutex_lock(&bq->event_lock);
	if (bq->event)
		bq->event(bq->event_data);
	else
		ret = bq25970_disable(bq->map);
	mutex_unlock(&bq->event_lock);
	if (ret)
		dev_err_ratelimited(bq->aux.dev.parent, "unowned pump stop failed: %d\n", ret);
	/* Latched faults are consumed only by the policy's serialized worker. */
	return IRQ_HANDLED;
}
static void bq25970_release(struct device *dev)
{
	struct auxiliary_device *aux = to_auxiliary_dev(dev);

	ida_free(&bq25970_ids, aux->id);
	fwnode_handle_put(dev_fwnode(dev));
	kfree(to_bq25970_device(aux));
}
static void bq25970_delete(void *data)
{
	struct bq25970_device *bq = data;

	/* Unbind policy while IRQ and regmap are still alive. */
	auxiliary_device_delete(&bq->aux);
}
static void bq25970_uninit(void *data)
{
	struct bq25970_device *bq = data;

	auxiliary_device_uninit(&bq->aux);
}
static const struct regmap_config bq25970_regmap = {
	.reg_bits = 8, .val_bits = 8, .max_register = 0x2e,
	.cache_type = REGCACHE_NONE,
};
static int bq25970_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct bq25970_device *bq;
	struct regmap *map;
	int ret, id;

	if (client->irq <= 0)
		return dev_err_probe(dev, -EINVAL, "fault IRQ is required\n");
	map = devm_regmap_init_i2c(client, &bq25970_regmap);
	if (IS_ERR(map))
		return PTR_ERR(map);
	ret = bq25970_check_identity(map);
	if (!ret)
		ret = bq25970_disable(map);
	if (ret)
		return dev_err_probe(dev, ret, "identity/initial pump stop failed\n");
	bq = kzalloc(sizeof(*bq), GFP_KERNEL);
	if (!bq)
		return -ENOMEM;
	id = ida_alloc(&bq25970_ids, GFP_KERNEL);
	if (id < 0) {
		kfree(bq);
		return id;
	}
	bq->map = map;
	mutex_init(&bq->event_lock);
	bq->aux.name = "charger";
	bq->aux.id = id;
	bq->aux.dev.parent = dev;
	bq->aux.dev.release = bq25970_release;
	device_set_node(&bq->aux.dev, fwnode_handle_get(dev_fwnode(dev)));
	ret = auxiliary_device_init(&bq->aux);
	if (ret) {
		fwnode_handle_put(dev_fwnode(&bq->aux.dev));
		ida_free(&bq25970_ids, id);
		kfree(bq);
		return ret;
	}
	/* LIFO: delete child, free IRQ, then release auxiliary allocation. */
	ret = devm_add_action_or_reset(dev, bq25970_uninit, bq);
	if (ret)
		return ret;
	ret = devm_request_threaded_irq(dev, client->irq, NULL, bq25970_irq,
			IRQF_ONESHOT, dev_name(dev), bq);
	if (ret)
		return ret;
	ret = auxiliary_device_add(&bq->aux);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(dev, bq25970_delete, bq);
	if (ret)
		return ret;
	i2c_set_clientdata(client, bq);
	return 0;
}
static void bq25970_shutdown(struct i2c_client *client)
{
	struct bq25970_device *bq = i2c_get_clientdata(client);
	int ret = bq25970_disable(bq->map);

	if (ret)
		dev_err(&client->dev, "shutdown pump stop failed: %d\n", ret);
}
static const struct of_device_id bq25970_match[] = {
	{ .compatible = "xiaomi,umi-bq25970" }, { }
};
MODULE_DEVICE_TABLE(of, bq25970_match);
static struct i2c_driver bq25970_driver = {
	.probe = bq25970_probe, .shutdown = bq25970_shutdown,
	.driver = { .name = "bq25970", .of_match_table = bq25970_match },
};
module_i2c_driver(bq25970_driver);
MODULE_DESCRIPTION("BQ25970 standalone chip transport and charge-pump function");
MODULE_AUTHOR("Texas Instruments; MiSeed contributors");
MODULE_LICENSE("GPL");

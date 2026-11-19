// SPDX-License-Identifier: GPL-2.0-only
/* Compatibility adapter for the existing Umi DT. Only this board layer
 * instantiates the Umi coordinator; the generic BQ driver never does so.
 * Keeps the current thermal/provider topology until its binding is replaced.
 */
#include <linux/i2c.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/property.h>
#include "bq25970_device.h"
#include "umi_bq25970.h"

static DEFINE_IDA(umi_bq_ids);
static void umi_bq_release(struct device *dev)
{
	struct auxiliary_device *aux = to_auxiliary_dev(dev);

	ida_free(&umi_bq_ids, aux->id);
	fwnode_handle_put(dev_fwnode(dev));
	kfree(container_of(aux, struct umi_bq25970, aux));
}
static void umi_bq_delete(void *data)
{
	struct umi_bq25970 *bridge = data;

	/* Drain the coordinator while chip IRQ, power supply and map still live. */
	auxiliary_device_delete(&bridge->aux);
	auxiliary_device_uninit(&bridge->aux);
}
static int umi_bq_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct bq25970_device *chip;
	struct umi_bq25970 *bridge;
	int ret, id;

	if (client->irq <= 0)
		return dev_err_probe(dev, -EINVAL, "Umi fault IRQ is required\n");
	chip = bq25970_register(client);
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	ret = umi_bq25970_profile(dev, chip);
	if (ret)
		return ret;
	bridge = kzalloc(sizeof(*bridge), GFP_KERNEL);
	if (!bridge)
		return -ENOMEM;
	id = ida_alloc(&umi_bq_ids, GFP_KERNEL);
	if (id < 0) {
		kfree(bridge);
		return id;
	}
	bridge->chip = chip;
	bridge->aux.name = "charger";
	bridge->aux.id = id;
	bridge->aux.dev.parent = dev;
	bridge->aux.dev.release = umi_bq_release;
	device_set_node(&bridge->aux.dev, fwnode_handle_get(dev_fwnode(dev)));
	ret = auxiliary_device_init(&bridge->aux);
	if (ret) {
		fwnode_handle_put(dev_fwnode(&bridge->aux.dev));
		ida_free(&umi_bq_ids, id);
		kfree(bridge);
		return ret;
	}
	ret = auxiliary_device_add(&bridge->aux);
	if (ret) {
		auxiliary_device_uninit(&bridge->aux);
		return ret;
	}
	ret = devm_add_action_or_reset(dev, umi_bq_delete, bridge);
	if (ret)
		return ret;
	i2c_set_clientdata(client, chip);
	return 0;
}
static void umi_bq_shutdown(struct i2c_client *client)
{
	bq25970_shutdown_device(i2c_get_clientdata(client));
}
static const struct of_device_id umi_bq_match[] = {
	{ .compatible = "xiaomi,umi-bq25970" }, { }
};
MODULE_DEVICE_TABLE(of, umi_bq_match);
struct i2c_driver umi_bq25970_driver = {
	.probe = umi_bq_probe, .shutdown = umi_bq_shutdown,
	.driver = { .name = "umi-bq25970", .of_match_table = umi_bq_match },
};

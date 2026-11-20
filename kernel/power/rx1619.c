// SPDX-License-Identifier: GPL-2.0-only
/* Umi RX1619 monitor. Register layout and ADC scales from colin's
 * NUVOLTA driver in LineageOS android_kernel_xiaomi_sm8250 at 71b13e62.
 */
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>

#define RX1619_VOUT	0x0009
#define RX1619_IOUT	0x000b

struct rx1619 {
	struct regmap *regmap;
	struct gpio_desc *power_good;
	struct gpio_desc *enable_gpio;
	struct power_supply *supply;
};

static enum power_supply_property rx1619_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

static int rx1619_get_property(struct power_supply *supply,
			       enum power_supply_property property,
			       union power_supply_propval *value)
{
	struct rx1619 *rx = power_supply_get_drvdata(supply);
	unsigned int reg, full_scale, raw;
	int ret;

	ret = gpiod_get_value_cansleep(rx->power_good);
	if (ret < 0)
		return ret;
	if (property == POWER_SUPPLY_PROP_ONLINE) {
		value->intval = ret;
		return 0;
	}
	if (!ret)
		return -ENODATA;

	switch (property) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		reg = RX1619_VOUT;
		full_scale = 22500000; /* microvolts */
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		reg = RX1619_IOUT;
		full_scale = 2500000; /* microamps */
		break;
	default:
		return -EINVAL;
	}
	ret = regmap_read(rx->regmap, reg, &raw);
	if (ret)
		return ret;
	/* Once power-good drops the same registers cease to describe RX. */
	ret = gpiod_get_value_cansleep(rx->power_good);
	if (ret <= 0)
		return ret ? ret : -ENODATA;
	value->intval = (u64)raw * full_scale / 256U;
	return 0;
}

static const struct regmap_config rx1619_regmap = {
	.reg_bits = 16,
	.val_bits = 8,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = RX1619_IOUT,
};

static const struct power_supply_desc rx1619_supply = {
	.name = "rx1619",
	.type = POWER_SUPPLY_TYPE_WIRELESS,
	.properties = rx1619_properties,
	.num_properties = ARRAY_SIZE(rx1619_properties),
	.get_property = rx1619_get_property,
};

static irqreturn_t rx1619_power_good_changed(int irq, void *data)
{
	struct rx1619 *rx = data;

	power_supply_changed(rx->supply);
	return IRQ_HANDLED;
}

static int rx1619_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct power_supply_config config = { .fwnode = dev_fwnode(dev) };
	struct rx1619 *rx;
	int irq, ret;

	rx = devm_kzalloc(dev, sizeof(*rx), GFP_KERNEL);
	if (!rx)
		return -ENOMEM;
	rx->regmap = devm_regmap_init_i2c(client, &rx1619_regmap);
	if (IS_ERR(rx->regmap))
		return dev_err_probe(dev, PTR_ERR(rx->regmap), "RX register map\n");
	if (IS_ENABLED(CONFIG_MISEED_RX_ENABLE_EXPERIMENT)) {
		rx->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
		if (IS_ERR(rx->enable_gpio))
			return dev_err_probe(dev, PTR_ERR(rx->enable_gpio), "RX enable GPIO\n");
	}
	rx->power_good = devm_gpiod_get(dev, "power-good", GPIOD_IN);
	if (IS_ERR(rx->power_good))
		return dev_err_probe(dev, PTR_ERR(rx->power_good), "RX power-good GPIO\n");
	irq = gpiod_to_irq(rx->power_good);
	if (irq < 0)
		return dev_err_probe(dev, irq, "RX power-good IRQ\n");
	config.drv_data = rx;
	rx->supply = devm_power_supply_register(dev, &rx1619_supply, &config);
	if (IS_ERR(rx->supply))
		return dev_err_probe(dev, PTR_ERR(rx->supply), "RX power supply\n");
	ret = devm_request_threaded_irq(dev, irq, NULL, rx1619_power_good_changed,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING, dev_name(dev), rx);
	if (ret)
		return dev_err_probe(dev, ret, "listen for RX power-good changes\n");
	i2c_set_clientdata(client, rx);
	power_supply_changed(rx->supply);
	return 0;
}

static int rx1619_resume(struct device *dev)
{
	struct rx1619 *rx = dev_get_drvdata(dev);

	power_supply_changed(rx->supply);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(rx1619_pm, NULL, rx1619_resume);

static const struct of_device_id rx1619_match[] = {
	{ .compatible = "xiaomi,umi-rx1619" },
	{ }
};
MODULE_DEVICE_TABLE(of, rx1619_match);

static struct i2c_driver rx1619_driver = {
	.driver = {
		.name = "rx1619",
		.of_match_table = rx1619_match,
		.pm = pm_sleep_ptr(&rx1619_pm),
	},
	.probe = rx1619_probe,
};
module_i2c_driver(rx1619_driver);

MODULE_DESCRIPTION("Umi RX1619 wireless power monitor");
MODULE_LICENSE("GPL");

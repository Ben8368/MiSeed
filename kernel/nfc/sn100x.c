// SPDX-License-Identifier: GPL-2.0-only
/*
 * NXP SN100T I2C transport for the Linux NCI core.
 * Framing based on drivers/nfc/nxp-nci/i2c.c in Linux f45ed76d6791:
 * Copyright (C) 2014 NXP Semiconductors. All rights reserved.
 * Copyright (C) 2012-2015 Intel Corporation. All rights reserved.
 * Authors: Clément Perrochaud <clement.perrochaud@nxp.com>
 *          Oleg Zhurakivskyy <oleg.zhurakivskyy@intel.com>
 *
 * SN100 NFCC and eSE share VEN. Never pulse or lower VEN on probe,
 * close, error, suspend or remove. No firmware download or eSE API.
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <net/nfc/nci_core.h>

#define SN100X_FRAME_MAX (NCI_CTRL_HDR_SIZE + U8_MAX)
#define SN100X_PROTOCOLS (NFC_PROTO_JEWEL_MASK | NFC_PROTO_MIFARE_MASK | \
			 NFC_PROTO_FELICA_MASK | NFC_PROTO_ISO14443_MASK | \
			 NFC_PROTO_ISO14443_B_MASK)

struct sn100x {
	struct i2c_client *client;
	struct nci_dev *ndev;
	struct gpio_desc *enable;
	struct gpio_desc *firmware;
	/* Serializes transport, IRQ state, open/close and PM transitions. */
	struct mutex lock;
	bool opened;
	bool irq_enabled;
	bool suspended;
	bool reset_pending;
	bool reset_acked;
	bool reset_confirmed;
	int fault;
};

/* Caller holds lock. A partial frame cannot safely be retried. */
static void sn100x_fault(struct sn100x *phy, int error)
{
	phy->fault = error;
	if (phy->irq_enabled) {
		phy->irq_enabled = false;
		disable_irq_nosync(phy->client->irq);
	}
	dev_err_ratelimited(&phy->client->dev,
			    "transport stopped: %d; VEN retained\n", error);
}

static int sn100x_read(struct sn100x *phy, struct sk_buff **frame)
{
	struct sk_buff *skb;
	u8 length;
	int ret;

	*frame = NULL;
	/* I2C buffers must be DMA safe, including the header. */
	skb = alloc_skb(SN100X_FRAME_MAX, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;
	ret = i2c_master_recv(phy->client, skb_put(skb, NCI_CTRL_HDR_SIZE),
			      NCI_CTRL_HDR_SIZE);
	if (ret != NCI_CTRL_HDR_SIZE)
		goto failed;
	length = skb->data[2];
	if (length) {
		ret = i2c_master_recv(phy->client, skb_put(skb, length), length);
		if (ret != length)
			goto failed;
	}
	*frame = skb;
	return 0;
failed:
	kfree_skb(skb);
	return ret < 0 ? ret : -EBADMSG;
}

/* Observe reset completion for safe close; NCI core owns the protocol. */
static void sn100x_reset_response(struct sn100x *phy, const struct sk_buff *skb)
{
	const u8 *p = skb->data;

	if (!phy->reset_pending)
		return;
	if (skb->len >= 4 && p[0] == 0x40 && p[1] == 0 && p[3] == 0) {
		if (skb->len == 4)
			phy->reset_acked = true; /* NCI 2.x also requires NTF. */
		else if (skb->len == 6 && (p[4] & 0xf0) == 0x10)
			phy->reset_confirmed = true;
	} else if (phy->reset_acked && skb->len >= 12 &&
		   p[0] == 0x60 && p[1] == 0 && p[3] == 2 &&
		   (p[5] & 0xf0) == 0x20 && p[7] == skb->len - 8) {
		phy->reset_confirmed = true;
	}
}

static irqreturn_t sn100x_irq(int irq, void *data)
{
	struct sn100x *phy = data;
	struct sk_buff *skb;
	int ret;

	mutex_lock(&phy->lock);
	if (!phy->opened || phy->fault)
		goto out;
	ret = sn100x_read(phy, &skb);
	if (ret)
		sn100x_fault(phy, ret);
	else {
		sn100x_reset_response(phy, skb);
		nci_recv_frame(phy->ndev, skb);
	}
out:
	mutex_unlock(&phy->lock);
	return IRQ_HANDLED;
}

static int sn100x_open(struct nci_dev *ndev)
{
	struct sn100x *phy = nci_get_drvdata(ndev);
	int ret;

	mutex_lock(&phy->lock);
	if (phy->suspended || phy->opened) {
		ret = -EBUSY;
		goto out;
	}
	if (phy->fault) {
		ret = phy->fault;
		goto out;
	}
	/* Refuse inherited download mode; never initiate a firmware update. */
	ret = gpiod_get_value_cansleep(phy->firmware);
	if (ret) {
		if (ret > 0)
			ret = -EBUSY;
		goto out;
	}
	ret = gpiod_direction_output(phy->firmware, 0);
	if (ret)
		goto out;
	ret = gpiod_direction_output(phy->enable, 1);
	if (ret)
		goto out;
	/* Existing SN100 downstream power-on settling time; no VEN reset. */
	usleep_range(10000, 15000);
	phy->opened = true;
	phy->irq_enabled = true;
	enable_irq(phy->client->irq);
out:
	mutex_unlock(&phy->lock);
	return ret;
}

static int sn100x_close(struct nci_dev *ndev)
{
	struct sn100x *phy = nci_get_drvdata(ndev);

	mutex_lock(&phy->lock);
	if (phy->opened && !phy->reset_confirmed && !phy->fault)
		sn100x_fault(phy, -ETIMEDOUT);
	phy->opened = false;
	if (phy->irq_enabled) {
		phy->irq_enabled = false;
		disable_irq_nosync(phy->client->irq);
	}
	mutex_unlock(&phy->lock);
	/* Never wait for the threaded handler while holding its mutex. */
	synchronize_irq(phy->client->irq);
	/*
	 * NCI core performs CORE_RESET before close; shared VEN stays high.
	 * A transport fault remains latched. No unverified power-cycle recovery.
	 */
	return 0;
}

static int sn100x_send(struct nci_dev *ndev, struct sk_buff *skb)
{
	struct sn100x *phy = nci_get_drvdata(ndev);
	int ret;

	mutex_lock(&phy->lock);
	if (phy->fault) {
		ret = phy->fault;
	} else if (!phy->opened || phy->suspended) {
		ret = -ENETDOWN;
	} else if (skb->len < NCI_CTRL_HDR_SIZE ||
		   skb->len > SN100X_FRAME_MAX ||
		   skb->len != NCI_CTRL_HDR_SIZE + skb->data[2]) {
		ret = -EINVAL;
	} else {
		phy->reset_pending = skb->len == 4 && skb->data[0] == 0x20 &&
				     skb->data[1] == 0;
		phy->reset_acked = false;
		phy->reset_confirmed = false;
		ret = i2c_master_send(phy->client, skb->data, skb->len);
		if (ret == skb->len) {
			ret = 0;
		} else {
			if (ret >= 0)
				ret = -EREMOTEIO;
			sn100x_fault(phy, ret);
		}
	}
	mutex_unlock(&phy->lock);
	kfree_skb(skb);
	return ret;
}

static const struct nci_ops sn100x_ops = {
	.open = sn100x_open,
	.close = sn100x_close,
	.send = sn100x_send,
};

static int sn100x_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct sn100x *phy;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;
	if (client->irq <= 0)
		return -EINVAL;
	if (irq_get_trigger_type(client->irq) != IRQ_TYPE_LEVEL_HIGH)
		return dev_err_probe(dev, -EINVAL, "level-high IRQ required\n");
	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;
	phy->client = client;
	mutex_init(&phy->lock);
	/* Acquiring descriptors must not reset the shared NFCC/eSE supply. */
	phy->enable = devm_gpiod_get(dev, "enable", GPIOD_ASIS);
	if (IS_ERR(phy->enable))
		return dev_err_probe(dev, PTR_ERR(phy->enable), "VEN GPIO\n");
	phy->firmware = devm_gpiod_get(dev, "firmware", GPIOD_ASIS);
	if (IS_ERR(phy->firmware))
		return dev_err_probe(dev, PTR_ERR(phy->firmware), "FIRM GPIO\n");

	phy->ndev = nci_allocate_device(&sn100x_ops, SN100X_PROTOCOLS, 0, 0);
	if (!phy->ndev)
		return -ENOMEM;
	nci_set_drvdata(phy->ndev, phy);
	nci_set_parent_dev(phy->ndev, dev);
	i2c_set_clientdata(client, phy);
	/* Register IRQ before publishing nfcX, but keep it disabled until open. */
	ret = request_threaded_irq(client->irq, NULL, sn100x_irq,
				   IRQF_ONESHOT | IRQF_NO_AUTOEN,
				   dev_name(dev), phy);
	if (ret)
		goto free_nci;
	ret = nci_register_device(phy->ndev);
	if (ret)
		goto free_irq;
	return 0;
free_irq:
	free_irq(client->irq, phy);
free_nci:
	nci_free_device(phy->ndev);
	return ret;
}

static void sn100x_remove(struct i2c_client *client)
{
	struct sn100x *phy = i2c_get_clientdata(client);

	/* Unregister may invoke close; do not hold the transport mutex. */
	nci_unregister_device(phy->ndev);
	free_irq(client->irq, phy);
	nci_free_device(phy->ndev);
}

static int sn100x_suspend(struct device *dev)
{
	struct sn100x *phy = i2c_get_clientdata(to_i2c_client(dev));
	int ret = 0;

	mutex_lock(&phy->lock);
	/* No unverified wake or RF-retention promise. User must down NFC first. */
	if (phy->opened || phy->fault)
		ret = -EBUSY;
	else
		phy->suspended = true;
	mutex_unlock(&phy->lock);
	return ret;
}

static int sn100x_resume(struct device *dev)
{
	struct sn100x *phy = i2c_get_clientdata(to_i2c_client(dev));

	mutex_lock(&phy->lock);
	phy->suspended = false;
	mutex_unlock(&phy->lock);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(sn100x_pm_ops, sn100x_suspend, sn100x_resume);

static const struct of_device_id sn100x_of_match[] = {
	{ .compatible = "nxp,sn100t" },
	{ }
};
MODULE_DEVICE_TABLE(of, sn100x_of_match);

static struct i2c_driver sn100x_driver = {
	.driver = {
		.name = "sn100x",
		.of_match_table = sn100x_of_match,
		.pm = pm_sleep_ptr(&sn100x_pm_ops),
	},
	.probe = sn100x_probe,
	.remove = sn100x_remove,
};
module_i2c_driver(sn100x_driver);

MODULE_DESCRIPTION("NXP SN100T NFC NCI I2C transport");
MODULE_LICENSE("GPL");

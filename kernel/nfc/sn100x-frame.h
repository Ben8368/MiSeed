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
#ifndef SN100X_FRAME_H
#define SN100X_FRAME_H

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
	if (ret != NCI_CTRL_HDR_SIZE) {
		dev_err_ratelimited(&phy->client->dev,
				    "I2C NCI header read failed: %d\n", ret);
		goto failed;
	}
	length = skb->data[2];
	if (length) {
		ret = i2c_master_recv(phy->client, skb_put(skb, length), length);
		if (ret != length) {
			dev_err_ratelimited(&phy->client->dev,
					    "I2C NCI payload read failed (%u bytes): %d\n",
					    length, ret);
			goto failed;
		}
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
		if (skb->len == 4) {
			phy->reset_acked = true; /* NCI 2.x also requires NTF. */
			dev_info(&phy->client->dev, "CORE_RESET response accepted (NCI 2.x)\n");
		} else if (skb->len == 6 && (p[4] & 0xf0) == 0x10) {
			phy->reset_confirmed = true;
			dev_info(&phy->client->dev, "CORE_RESET response accepted (NCI 1.x)\n");
		}
	} else if (phy->reset_acked && skb->len >= 12 &&
		   p[0] == 0x60 && p[1] == 0 && p[3] == 2 &&
		   (p[5] & 0xf0) == 0x20 && p[7] == skb->len - 8) {
		dev_info(&phy->client->dev, "CORE_RESET notification accepted\n");
		phy->reset_confirmed = true;
	}
}

/* Caller holds lock. Consume the probe-time allowance even for a non-reset. */
static int sn100x_write(struct sn100x *phy, const struct sk_buff *skb)
{
	bool initial_reset = phy->geni_address_nack &&
			     phy->retry_first_reset && phy->reset_pending;
	/* Unfragmented RF_DEACTIVATE_CMD with the idle parameter. */
	bool deactivate_idle = phy->geni_address_nack && skb->len == 4 &&
			       skb->data[0] == 0x21 && skb->data[1] == 0x06 &&
			       skb->data[3] == NCI_DEACTIVATE_TYPE_IDLE_MODE;
	unsigned int attempts = initial_reset ? 3 : deactivate_idle ? 2 : 1;
	unsigned int attempt;
	int ret;

	phy->retry_first_reset = false;
	for (attempt = 1; ; attempt++) {
		ret = i2c_master_send(phy->client, skb->data, skb->len);
		if (initial_reset)
			dev_info(&phy->client->dev, "initial CORE_RESET attempt=%u ret=%d\n",
				 attempt, ret);
		if (deactivate_idle && (ret == -ENXIO || attempt > 1))
			dev_info(&phy->client->dev, "RF_DEACTIVATE idle attempt=%u ret=%d\n",
				 attempt, ret);
		if (ret != -ENXIO || attempt == attempts)
			return ret;
		usleep_range(1000, 1100);
	}
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
		ret = sn100x_write(phy, skb);
		if (ret == skb->len) {
			if (phy->reset_pending)
				dev_info(&phy->client->dev,
					 "NCI CORE_RESET I2C write accepted (%u bytes)\n",
					 skb->len);
			ret = 0;
		} else {
			/* Identify the failing command without logging tag/APDU data. */
			if (nci_mt(skb->data) == NCI_MT_CMD_PKT)
				dev_err_ratelimited(&phy->client->dev,
					"I2C NCI command write failed: opcode=%#06x len=%u ret=%d\n",
					nci_opcode(skb->data), skb->len, ret);
			else
				dev_err_ratelimited(&phy->client->dev,
					"I2C NCI non-command write failed: mt=%u len=%u ret=%d\n",
					nci_mt(skb->data), skb->len, ret);
			if (ret >= 0)
				ret = -EREMOTEIO;
			sn100x_fault(phy, ret);
		}
	}
	mutex_unlock(&phy->lock);
	kfree_skb(skb);
	return ret;
}

#endif

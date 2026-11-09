// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023, Linaro Ltd. All rights reserved.
 * Adapted from Linux f45ed76d6791cf72ff1265ee93511175bbbc2f18,
 * drivers/usb/typec/tcpm/qcom/qcom_pmic_typec_pdphy.c.
 * MiSeed: reject invalid RX lengths, release their buffer, and log faults.
 */
static void qcom_pmic_typec_pdphy_pd_receive(struct pmic_typec_pdphy *pmic_typec_pdphy)
{
	struct device *dev = pmic_typec_pdphy->dev;
	struct pd_message msg = { 0 };
	unsigned int size = 0, rx_status = 0;
	unsigned long flags;
	int ret, ack_ret;

	spin_lock_irqsave(&pmic_typec_pdphy->lock, flags);

	ret = regmap_read(pmic_typec_pdphy->regmap,
			  pmic_typec_pdphy->base + USB_PDPHY_RX_SIZE_REG, &size);
	if (ret)
		goto done;

	/* Hardware encodes the wire length minus one. Never forward an unread
	 * message on this error path, even if the register access succeeded.
	 */
	if (size < 1 || size > sizeof(msg.payload) + 1) {
		dev_warn_ratelimited(dev, "miseed-pdphy invalid_rx_size=%u\n", size);
		ret = -EINVAL;
		goto release_rx;
	}

	size += 1;
	ret = regmap_read(pmic_typec_pdphy->regmap,
			  pmic_typec_pdphy->base + USB_PDPHY_RX_STATUS_REG,
			  &rx_status);
	if (ret)
		goto done;

	ret = regmap_bulk_read(pmic_typec_pdphy->regmap,
			       pmic_typec_pdphy->base + USB_PDPHY_RX_BUFFER_REG,
			       (u8 *)&msg, size);
	if (ret)
		goto done;

release_rx:
	/* Release both a consumed valid frame and a rejected invalid length.
	 * An ACK failure must not turn either path into a successful receive.
	 */
	ack_ret = regmap_write(pmic_typec_pdphy->regmap,
			      pmic_typec_pdphy->base + USB_PDPHY_RX_ACKNOWLEDGE_REG, 0);
	if (ack_ret)
		ret = ack_ret;
done:
	spin_unlock_irqrestore(&pmic_typec_pdphy->lock, flags);

	if (ret) {
		dev_warn_ratelimited(dev, "miseed-pdphy rx_error=%d size=%u status=0x%x\n",
				     ret, size, rx_status);
		return;
	}
	/* A genuinely read zero header is evidence, not permission to bypass
	 * TCPM's role/protocol validation. Keep the ordinary receive path.
	 */
	if (!msg.header)
		dev_warn_ratelimited(dev, "miseed-pdphy zero_rx_header size=%u status=0x%x\n",
				     size, rx_status);
	tcpm_pd_receive(pmic_typec_pdphy->tcpm_port, &msg, TCPC_TX_SOP);
}

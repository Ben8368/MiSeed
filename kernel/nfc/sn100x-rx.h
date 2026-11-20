// SPDX-License-Identifier: GPL-2.0-only
#ifndef SN100X_RX_H
#define SN100X_RX_H

#ifdef CONFIG_MISEED_RX_NFC_COEX
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/workqueue.h>

#define SN100X_RX_REFERENCE "nxp,rf-interference-source"

struct sn100x_rx {
	struct device *dev;
	struct nci_dev *ndev;
	struct power_supply *source;
	struct notifier_block notifier;
	struct delayed_work work;
};

static void sn100x_rx_work(struct work_struct *work)
{
	struct sn100x_rx *rx = container_of(to_delayed_work(work), struct sn100x_rx, work);
	union power_supply_propval online;
	int ret, rf_ret;

	ret = power_supply_get_property(rx->source, POWER_SUPPLY_PROP_ONLINE, &online);
	rf_ret = nci_set_rf_inhibited(rx->ndev, ret || online.intval);
	if (ret || rf_ret) {
		dev_warn_ratelimited(rx->dev,
				     "RX/NFC state unconfirmed: RX=%d RF=%d\n", ret, rf_ret);
		/* An edge queued during this attempt takes priority over the retry. */
		queue_delayed_work(system_freezable_wq, &rx->work, HZ);
	}
}

static int sn100x_rx_changed(struct notifier_block *notifier,
			     unsigned long event, void *data)
{
	struct sn100x_rx *rx = container_of(notifier, struct sn100x_rx, notifier);

	if (event != PSY_EVENT_PROP_CHANGED || data != rx->source)
		return NOTIFY_DONE;
	mod_delayed_work(system_freezable_wq, &rx->work, 0);
	return NOTIFY_OK;
}

static int sn100x_rx_init(struct sn100x_rx *rx, struct device *dev,
			 struct nci_dev *ndev)
{
	if (!device_property_present(dev, SN100X_RX_REFERENCE))
		return 0;
	rx->source = devm_power_supply_get_by_reference(dev, SN100X_RX_REFERENCE);
	if (IS_ERR(rx->source))
		return PTR_ERR(rx->source);
	if (!rx->source)
		return -EPROBE_DEFER;
	if (!device_link_add(dev, rx->source->dev.parent, DL_FLAG_AUTOREMOVE_CONSUMER))
		return -ENOMEM;
	rx->dev = dev;
	rx->ndev = ndev;
	rx->notifier.notifier_call = sn100x_rx_changed;
	INIT_DELAYED_WORK(&rx->work, sn100x_rx_work);
	/* Keep RF gated until the first post-registration sample. */
	return nci_set_rf_inhibited(ndev, true);
}

static int sn100x_rx_start(struct sn100x_rx *rx)
{
	int ret;

	if (!rx->source)
		return 0;
	ret = power_supply_reg_notifier(&rx->notifier);
	if (ret)
		return ret;
	mod_delayed_work(system_freezable_wq, &rx->work, 0);
	return 0;
}

static void sn100x_rx_stop(struct sn100x_rx *rx)
{
	if (!rx->source)
		return;
	power_supply_unreg_notifier(&rx->notifier);
	cancel_delayed_work_sync(&rx->work);
}

#else
struct sn100x_rx {};
static inline int sn100x_rx_init(struct sn100x_rx *rx, struct device *dev, struct nci_dev *ndev)
{
	return 0;
}
static inline int sn100x_rx_start(struct sn100x_rx *rx) { return 0; }
static inline void sn100x_rx_stop(struct sn100x_rx *rx) {}
#endif
#endif

// SPDX-License-Identifier: GPL-2.0-only
/* Included by NCI core beside the RF deactivate request. */

/* The caller serializes state changes and keeps ndev registered. */
int nci_set_rf_inhibited(struct nci_dev *ndev, bool inhibit)
{
	struct nfc_dev *nfc = ndev->nfc_dev;
	bool deactivate, target_active, target_mode;
	u32 target = 0;
	int ret, previous;

	/* Serialize the gate with start-poll, target activation and exchange. */
	device_lock(&nfc->dev);
	if (inhibit)
		WRITE_ONCE(ndev->rf_inhibited, true);
	previous = atomic_read(&ndev->state);
	deactivate = nfc->dev_up && ndev->rf_inhibited &&
		(ndev->rf_deactivate_pending || previous != NCI_IDLE);
	ndev->rf_deactivate_pending = deactivate;
	device_unlock(&nfc->dev);

	if (deactivate) {
		/* Discovery notifications may need the NFC device lock. */
		ret = nci_request(ndev, nci_rf_deactivate_req,
				  (void *)NCI_DEACTIVATE_TYPE_IDLE_MODE,
				  msecs_to_jiffies(NCI_RF_DEACTIVATE_TIMEOUT));
		if (ret && ret != -ENETDOWN)
			return ret;
		if (!ret && atomic_read(&ndev->state) != NCI_IDLE)
			return -EIO;
		if (!ret)
			pr_info("miseed-rx-nfc rf_idle previous=%s\n",
				previous == NCI_POLL_ACTIVE ? "target" :
				previous == NCI_DISCOVERY || previous == NCI_W4_ALL_DISCOVERIES ?
				"discovery" : "other");
	}

	device_lock(&nfc->dev);
	ndev->rf_deactivate_pending = false;
	if (!ndev->rf_inhibited) {
		device_unlock(&nfc->dev);
		return 0;
	}
	ndev->target_active_prot = 0;
	target_active = !!nfc->active_target;
	if (target_active)
		target = nfc->active_target->idx;
	target_mode = nfc->rf_mode == NFC_RF_TARGET;
	device_unlock(&nfc->dev);

	if (target_mode)
		nfc_tm_deactivated(nfc);
	if (target_active)
		nfc_target_lost(nfc, target);
	/* Complete any outstanding userspace poll through its normal event. */
	nfc_targets_found(nfc, NULL, 0);

	if (!inhibit) {
		device_lock(&nfc->dev);
		WRITE_ONCE(ndev->rf_inhibited, false);
		device_unlock(&nfc->dev);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(nci_set_rf_inhibited);

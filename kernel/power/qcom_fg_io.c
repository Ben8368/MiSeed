// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020, The Linux Foundation. All rights reserved. */

#include "qcom_fg_internal.h"

/************************
 * IO FUNCTIONS
 * **********************/

/**
 * @brief qcom_fg_read() - Read multiple registers with regmap_bulk_read
 *
 * @param chip Pointer to chip
 * @param val Pointer to read values into
 * @param addr Address to read from
 * @param len Number of registers (bytes) to read
 * @return int 0 on success, negative errno on error
 */
int qcom_fg_read(struct qcom_fg_chip *chip, u8 *val, u16 addr, int len)
{
	if (((chip->base + addr) & 0xff00) == 0)
		return -EINVAL;

	dev_vdbg(chip->dev, "%s: Reading 0x%x bytes from 0x%x", __func__, len, addr);

	return regmap_bulk_read(chip->regmap, chip->base + addr, val, len);
}

/**
 * @brief qcom_fg_write() - Write multiple registers with regmap_bulk_write
 *
 * @param chip Pointer to chip
 * @param val Pointer to write values from
 * @param addr Address to write to
 * @param len Number of registers (bytes) to write
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_write(struct qcom_fg_chip *chip, u8 *val, u16 addr, int len)
{
	bool sec_access = (addr & 0xff) > 0xd0;
	u8 sec_addr_val = 0xa5;
	int ret;

	if (((chip->base + addr) & 0xff00) == 0)
			return -EINVAL;

	dev_vdbg(chip->dev, "%s: Writing 0x%x to 0x%x", __func__, *val, addr);

	if (sec_access) {
		ret = regmap_bulk_write(chip->regmap,
				((chip->base + addr) & 0xff00) | 0xd0,
				&sec_addr_val, 1);
		if (ret)
			return ret;
	}

	return regmap_bulk_write(chip->regmap, chip->base + addr, val, len);
}

/**
 * @brief qcom_fg_masked_write() - like qcom_fg_write but applies
 * a mask first.
 *
 * @param chip Pointer to chip
 * @param val Pointer to write values from
 * @param addr Address to write to
 * @param len Number of registers (bytes) to write
 * @return int 0 on success, negative errno on error
 */
int qcom_fg_masked_write(struct qcom_fg_chip *chip, u16 addr, u8 mask, u8 val)
{
	u8 reg;
	int ret;

	ret = qcom_fg_read(chip, &reg, addr, 1);
	if (ret)
		return ret;

	reg &= ~mask;
	reg |= val & mask;

	return qcom_fg_write(chip, &reg, addr, 1);
}

/************************
 * SRAM FUNCTIONS
 * **********************/

/**
 * @brief qcom_fg_sram_check_access() - Check if SRAM is accessible
 *
 * @param chip Pointer to chip
 * @return bool true if accessible, false otherwise
 */
bool qcom_fg_sram_check_access(struct qcom_fg_chip *chip)
{
	u8 mem_if_status;
	int ret;

	ret = qcom_fg_read(chip, &mem_if_status,
		MEM_INTF_STS(chip), 1);

	if (ret || !(mem_if_status & MEM_INTF_AVAIL))
		return false;

	ret = qcom_fg_read(chip, &mem_if_status,
		MEM_INTF_CFG(chip), 1);

	if (ret)
		return false;

	return !!(mem_if_status & RIF_MEM_ACCESS_REQ);
}

/**
 * @brief qcom_fg_sram_request_access() - Request access to SRAM and wait for it
 *
 * @param chip Pointer to chip
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_sram_request_access(struct qcom_fg_chip *chip)
{
	bool sram_accessible;
	int ret;
	int clear_ret;

	/* Do not let a queued release revoke access during a new request. */
	cancel_delayed_work_sync(&chip->sram_release_access_work);

	spin_lock(&chip->sram_request_lock);

	sram_accessible = qcom_fg_sram_check_access(chip);

	dev_vdbg(chip->dev, "Requesting SRAM access, current state: %d, requests: %d\n",
		sram_accessible, chip->sram_requests);

	if (!sram_accessible && chip->sram_requests == 0) {
		/* A completion from an earlier request is not a new hardware grant. */
		reinit_completion(&chip->sram_access_granted);
		reinit_completion(&chip->sram_access_revoked);
		ret = qcom_fg_masked_write(chip, MEM_INTF_CFG(chip),
				RIF_MEM_ACCESS_REQ, RIF_MEM_ACCESS_REQ);
		if (ret) {
			dev_err(chip->dev,
				"Failed to set SRAM access request bit: %d\n", ret);

			spin_unlock(&chip->sram_request_lock);
			return ret;
		}
	}

	chip->sram_requests++;

	spin_unlock(&chip->sram_request_lock);
	if (sram_accessible)
		return 0;

	/* Wait to get access to SRAM, and try again if interrupted */
	do {
		ret = wait_for_completion_interruptible_timeout(
			&chip->sram_access_granted,
			msecs_to_jiffies(MEM_IF_TIMEOUT_MS));
	} while(ret == -ERESTARTSYS);

	if (ret <= 0) {
		ret = -ETIMEDOUT;

		spin_lock(&chip->sram_request_lock);
		chip->sram_requests--;
		if (!chip->sram_requests) {
			clear_ret = qcom_fg_masked_write(chip, MEM_INTF_CFG(chip),
					RIF_MEM_ACCESS_REQ, 0);
			if (clear_ret)
				dev_err(chip->dev,
					"Failed to clear timed-out SRAM request: %d\n",
					clear_ret);
		}
		spin_unlock(&chip->sram_request_lock);
	} else {
		ret = 0;

		reinit_completion(&chip->sram_access_revoked);
	}

	return ret;
}

/**
 * @brief qcom_fg_sram_release_access() - Release access to SRAM
 *
 * @param chip Pointer to chip
 * @return int 0 on success, negative errno on error
 */
static void qcom_fg_sram_release_access(struct qcom_fg_chip *chip)
{
	spin_lock(&chip->sram_request_lock);

	chip->sram_requests--;

	if(WARN(chip->sram_requests < 0,
			"sram_requests=%d, cannot be negative! resetting to 0.\n",
			chip->sram_requests))
		chip->sram_requests = 0;

	if(chip->sram_requests == 0)
		/* Schedule access release */
		queue_delayed_work(chip->sram_wq, &chip->sram_release_access_work,
			msecs_to_jiffies(SRAM_ACCESS_RELEASE_DELAY_MS));

	spin_unlock(&chip->sram_request_lock);
}

void qcom_fg_sram_release_access_worker(struct work_struct *work)
{
	struct qcom_fg_chip *chip;
	bool wait = false;
	int ret;

	chip = container_of(work, struct qcom_fg_chip, sram_release_access_work.work);

	spin_lock(&chip->sram_request_lock);

	/* Request access release if there are still no access requests */
	if(chip->sram_requests == 0) {
		qcom_fg_masked_write(chip, MEM_INTF_CFG(chip), RIF_MEM_ACCESS_REQ, 0);
		wait = true;
	}

	spin_unlock(&chip->sram_request_lock);

	if(!wait)
		return;

	/* Wait for SRAM access to be released, and try again if interrupted */
	do {
		ret = wait_for_completion_interruptible_timeout(
			&chip->sram_access_revoked,
			msecs_to_jiffies(MEM_IF_TIMEOUT_MS));
	} while(ret == -ERESTARTSYS);

	reinit_completion(&chip->sram_access_granted);
}

/**
 * @brief qcom_fg_sram_config_access() - Configure access to SRAM
 *
 * @param chip Pointer to chip
 * @param write 0 for read access, 1 for write access
 * @param burst 1 to access mutliple addresses successively
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_sram_config_access(struct qcom_fg_chip *chip,
		bool write, bool burst)
{
	u8 intf_ctl;
	int ret;

	intf_ctl = (write ? MEM_INTF_CTL_WR_EN : 0)
			| (burst ? MEM_INTF_CTL_BURST : 0);

	ret = qcom_fg_write(chip, &intf_ctl,
			MEM_INTF_CTL(chip), 1);
	if (ret) {
		dev_err(chip->dev, "Failed to configure SRAM access: %d\n", ret);
		return ret;
	}

	return 0;
}

/**
 * @brief qcom_fg_sram_read() - Read data from SRAM
 *
 * @param chip Pointer to chip
 * @param val Pointer to read values into
 * @param addr Address to read from
 * @param len Number of bytes to read
 * @return int 0 on success, negative errno on error
 */
int qcom_fg_sram_read(struct qcom_fg_chip *chip,
		u8 *val, u16 addr, int len, int offset)
{
	u8 *rd_data = val;
	int ret = 0;

	ret = qcom_fg_sram_request_access(chip);
	if (ret) {
		dev_err(chip->dev, "Failed to request SRAM access: %d", ret);
		return ret;
	}

	spin_lock(&chip->sram_rw_lock);

	dev_vdbg(chip->dev,
		"Reading address 0x%x with offset %d of length %d from SRAM",
		addr, len, offset);

	ret = qcom_fg_sram_config_access(chip, 0, (len > 4));
	if (ret) {
		dev_err(chip->dev, "Failed to configure SRAM access: %d", ret);
		goto out;
	}

	while(len > 0) {
		int read_len = min(len, 4 - offset);

		/* Set SRAM address register */
		ret = qcom_fg_write(chip, (u8 *) &addr,
				MEM_INTF_ADDR_LSB(chip), 2);
		if (ret) {
			dev_err(chip->dev, "Failed to set SRAM address: %d", ret);
			goto out;
		}

		ret = qcom_fg_read(chip, rd_data,
				MEM_INTF_RD_DATA0(chip) + offset, read_len);

		addr += 4;

		if (ret)
			goto out;

		rd_data += read_len;
		len -= read_len;
		offset = 0;
	}
out:
	spin_unlock(&chip->sram_rw_lock);
	qcom_fg_sram_release_access(chip);

	return ret;
}

/**
 * @brief qcom_fg_sram_write() - Write data to SRAM
 *
 * @param chip Pointer to chip
 * @param val Pointer to write values from
 * @param addr Address to write to
 * @param len Number of bytes to write
 * @return int 0 on success, negative errno on error
 */
int qcom_fg_sram_write(struct qcom_fg_chip *chip,
		u8 *val, u16 addr, int len, int offset)
{
	u8 *wr_data = val;
	int ret;

	ret = qcom_fg_sram_request_access(chip);
	if (ret) {
		dev_err(chip->dev, "Failed to request SRAM access: %d", ret);
		return ret;
	}

	spin_lock(&chip->sram_rw_lock);

	dev_vdbg(chip->dev,
		"Writing address 0x%x with offset %d of length %d to SRAM",
		addr, len, offset);

	ret = qcom_fg_sram_config_access(chip, 1, (len > 4));
	if (ret) {
		dev_err(chip->dev, "Failed to configure SRAM access: %d", ret);
		goto out;
	}

	while(len > 0) {
		int write_len = min(len, 4 - offset);

		/* Set SRAM address register */
		ret = qcom_fg_write(chip, (u8 *) &addr,
				MEM_INTF_ADDR_LSB(chip), 2);
		if (ret) {
			dev_err(chip->dev, "Failed to set SRAM address: %d", ret);
			goto out;
		}

		ret = qcom_fg_write(chip, wr_data,
				MEM_INTF_WR_DATA0(chip) + offset, write_len);

		addr += 4;

		if (ret)
			goto out;

		wr_data += write_len;
		len -= write_len;
		offset = 0;
	}
out:
	spin_unlock(&chip->sram_rw_lock);
	qcom_fg_sram_release_access(chip);

	return ret;
}

/********************
 * INIT FUNCTIONS
 * ******************/

static int qcom_fg_iacs_clear_sequence(struct qcom_fg_chip *chip)
{
	u8 temp;
	int ret;

	/* clear the error */
	ret = qcom_fg_masked_write(chip, MEM_INTF_IMA_CFG(chip), BIT(2), BIT(2));
	if (ret) {
		dev_err(chip->dev, "Failed to write IMA_CFG: %d\n", ret);
		return ret;
	}

	temp = 0x4;
	ret = qcom_fg_write(chip, &temp, MEM_INTF_ADDR_LSB(chip) + 1, 1);
	if (ret) {
		dev_err(chip->dev, "Failed to write MEM_INTF_ADDR_MSB: %d\n", ret);
		return ret;
	}

	temp = 0x0;
	ret = qcom_fg_write(chip, &temp, MEM_INTF_WR_DATA0(chip) + 3, 1);
	if (ret) {
		dev_err(chip->dev, "Failed to write WR_DATA3: %d\n", ret);
		return ret;
	}

	ret = qcom_fg_read(chip, &temp, MEM_INTF_RD_DATA0(chip) + 3, 1);
	if (ret) {
		dev_err(chip->dev, "Failed to write RD_DATA3: %d\n", ret);
		return ret;
	}

	ret = qcom_fg_masked_write(chip, MEM_INTF_IMA_CFG(chip), BIT(2), 0);
	if (ret) {
		dev_err(chip->dev, "Failed to write IMA_CFG: %d\n", ret);
		return ret;
	}

	return 0;
}

int qcom_fg_clear_ima(struct qcom_fg_chip *chip,
		bool check_hw_sts)
{
	u8 err_sts, exp_sts, hw_sts;
	bool run_err_clr_seq = false;
	int ret;

	ret = qcom_fg_read(chip, &err_sts,
			MEM_INTF_IMA_ERR_STS(chip), 1);
	if (ret) {
		dev_err(chip->dev, "Failed to read IMA_ERR_STS: %d\n", ret);
		return ret;
	}

	ret = qcom_fg_read(chip, &exp_sts,
			MEM_INTF_IMA_EXP_STS(chip), 1);
	if (ret) {
		dev_err(chip->dev, "Failed to read IMA_EXP_STS: %d\n", ret);
		return ret;
	}

	if (check_hw_sts) {
		ret = qcom_fg_read(chip, &hw_sts,
				MEM_INTF_IMA_HW_STS(chip), 1);
		if (ret) {
			dev_err(chip->dev, "Failed to read IMA_HW_STS: %d\n", ret);
			return ret;
		}
		/*
		 * Lower nibble should be equal to upper nibble before SRAM
		 * transactions begins from SW side.
		 */
		if ((hw_sts & 0x0f) != hw_sts >> 4) {
			dev_dbg(chip->dev, "IMA HW not in correct state, hw_sts=%x\n",
					hw_sts);
			run_err_clr_seq = true;
		}
	}

	if (exp_sts & (BIT(0) | BIT(1) | BIT(3) |
		BIT(4) | BIT(5) | BIT(6) |
		BIT(7))) {
		dev_dbg(chip->dev, "IMA exception bit set, exp_sts=%x\n", exp_sts);
		run_err_clr_seq = true;
	}

	if (run_err_clr_seq) {
		ret = qcom_fg_iacs_clear_sequence(chip);
		if (!ret)
			return -EAGAIN;
		return ret;
	}

	return 0;
}

/* SPDX-License-Identifier: GPL-2.0-only */
/* PMIC5 sequence from The Linux Foundation's qcom-spmi-adc5.c, LOS
 * 71b13e62 (ab8da1dd: separate configuration register writes).
 * Keep mainline IIO types, scaling, locking and ADC7 handling.
 */
static int adc5_configure(struct adc5_chip *adc,
			struct adc5_channel_prop *prop)
{
	u8 buf[5], selected, request = ADC5_USR_CONV_REQ_REQ;
	int ret, i;

	ret = adc5_read(adc, ADC5_USR_DIG_PARAM, buf, sizeof(buf));
	if (ret)
		return ret;
	adc5_update_dig_param(adc, prop, &buf[0]);
	buf[1] &= ~ADC5_USR_FAST_AVG_CTL_SAMPLES_MASK;
	buf[1] |= prop->avg_samples;
	buf[2] = prop->channel;
	buf[3] &= ~ADC5_USR_HW_SETTLE_DELAY_MASK;
	buf[3] |= prop->hw_settle_time;
	buf[4] |= ADC5_USR_EN_CTL1_ADC_EN;
	/* Finish all configuration writes before requesting any conversion. */
	for (i = 0; i < ARRAY_SIZE(buf); i++) {
		ret = adc5_write(adc, ADC5_USR_DIG_PARAM + i, &buf[i], 1);
		if (ret)
			return ret;
	}
	ret = adc5_read(adc, ADC5_USR_CH_SEL_CTL, &selected, 1);
	if (ret)
		return ret;
	if (selected != prop->channel)
		return -EIO;
	if (!adc->poll_eoc)
		reinit_completion(&adc->complete);
	return adc5_write(adc, ADC5_USR_CONV_REQ, &request, 1);
}

static int adc5_validate_result(struct adc5_chip *adc,
			       struct adc5_channel_prop *prop)
{
	u8 status, selected;
	int ret;

	ret = adc5_read(adc, ADC5_USR_STATUS1, &status, 1);
	if (ret)
		return ret;
	if ((status & ADC5_USR_STATUS1_CONV_FAULT) ||
	    (status & ADC5_USR_STATUS1_REQ_STS_EOC_MASK) != ADC5_USR_STATUS1_EOC)
		return -EIO;
	ret = adc5_read(adc, ADC5_USR_CH_SEL_CTL, &selected, 1);
	if (ret)
		return ret;
	if (selected != prop->channel)
		return -EIO;
	return 0;
}

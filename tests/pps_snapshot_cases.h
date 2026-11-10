// SPDX-License-Identifier: AGPL-3.0-only
/* Keep consumed fault evidence even when a later register read fails. */
static void test_partial_fault_snapshot(void)
{
	struct umi_pps_sample sample = {0};

	reset();
	assert(!umi_pps_prepare(&c));
	regs[0x10] = 0x20;
	regs[0x11] = 0x40;
	io_count = 0;
	fail_at = c.nprotection + 3; /* Fail alarm read after fault + latch. */
	assert(umi_pps_sample(&c, &sample) == -EIO);
	assert(sample.fault_raw == 0x20 && sample.latched_raw == 0x40);
}

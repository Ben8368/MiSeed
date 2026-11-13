/* SPDX-License-Identifier: GPL-2.0-only */
/* Temporary ABL-direct diagnostic identity quirk; see TD-006.
 * Reuse upstream SM8250 data without changing any UBWC hardware parameters.
 * Product UEFI handoff must restore the canonical DT identity instead.
 */
#if IS_ENABLED(CONFIG_DRM_PANEL_XIAOMI_UMI_CSOT)
#include <dt-bindings/arm/qcom,ids.h>

static bool miseed_umi_abl_ubwc_root(struct device_node *root)
{
	u32 board[2], msm[2];
	const char *model;

	if (of_property_count_strings(root, "compatible") != 3 ||
	    !of_device_is_compatible(root, "qcom,kona-mtp") ||
	    !of_device_is_compatible(root, "qcom,kona") ||
	    !of_device_is_compatible(root, "qcom,mtp") ||
	    of_property_count_strings(root, "model") != 1 ||
	    of_property_read_string(root, "model", &model) ||
	    strcmp(model, "Qualcomm Technologies, Inc. xiaomi umi") ||
	    of_property_count_u32_elems(root, "qcom,board-id") != 2 ||
	    of_property_read_u32_array(root, "qcom,board-id", board, 2) ||
	    of_property_count_u32_elems(root, "qcom,msm-id") != 2 ||
	    of_property_read_u32_array(root, "qcom,msm-id", msm, 2))
		return false;

	return board[0] == 0x2b && board[1] == 0 && msm[0] == QCOM_ID_SM8250;
}
#endif

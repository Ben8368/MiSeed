"""Stage fixed Umi CSOT panel; native display is explicitly opt-in."""
# SPDX-License-Identifier: AGPL-3.0-only
import argparse
import hashlib
from pathlib import Path
if __package__:
    from .apply_clock_handoff import CLOCK_PATH, transform_clock, stage_clock
    from .apply_power_port import REVISION, destination_dir
else:
    from apply_clock_handoff import CLOCK_PATH, transform_clock, stage_clock
    from apply_power_port import REVISION, destination_dir

UBWC_PATH = "drivers/soc/qcom/ubwc_config.c"
UBWC_SHA256 = "54351cbd9cad8293c312ef519fb227aeaa6d8f5abb90b0bbc694e7e8ea3752c0"
UBWC_CALL = '\tmatch = of_match_node(qcom_ubwc_configs, root);\n'
UBWC_QUIRK = '''#if IS_ENABLED(CONFIG_DRM_PANEL_XIAOMI_UMI_CSOT)
	if (!match && miseed_umi_abl_ubwc_root(root)) {
		of_node_put(root);
		pr_warn_once("MiSeed ABL Umi root: using standard SM8250 UBWC data\\n");
		return &sm8250_data;
	}
#endif
'''
DSI_PATH = "drivers/gpu/drm/msm/dsi/dsi_host.c"
DSI_SHA256 = "9afe6c379338c04f05df7d53cdcc7de1c3e6be9bc327fc0fb31954752439eba8"
DSI_TIMEOUT = '''#if IS_ENABLED(CONFIG_DRM_PANEL_XIAOMI_UMI_CSOT)
		if (!ret)
			dev_err(&msm_host->pdev->dev,
				"MiSeed DSI timeout: iova=%#llx len=%d ctrl=%#x status=%#x intr=%#x power=%d enabled=%d pclk=%lu byteclk=%lu escclk=%lu\\n",
				(unsigned long long)dma_base, len,
				dsi_read(msm_host, REG_DSI_CTRL),
				dsi_read(msm_host, REG_DSI_STATUS0),
				dsi_read(msm_host, REG_DSI_INTR_CTRL),
				msm_host->power_on, msm_host->enabled,
				msm_host->pixel_clk_rate, msm_host->byte_clk_rate,
				msm_host->esc_clk_rate);
#endif
'''
DSI_FIFO = '''#if IS_ENABLED(CONFIG_DRM_PANEL_XIAOMI_UMI_CSOT)
	if (status)
		dev_err_ratelimited(&msm_host->pdev->dev,
			"MiSeed DSI FIFO before clear: %#x\\n", status);
#endif
'''
DSI_INSERTIONS = {
    '\t\tDBG("ret=%d", ret);\n': DSI_TIMEOUT,
    '\tstatus = dsi_read(msm_host, REG_DSI_FIFO_STATUS);\n': DSI_FIFO,
}
MDSS_PATH = "drivers/gpu/drm/msm/msm_mdss.c"
MDSS_SHA256 = "645e876bdb2aec5553245692936887a0d3ab8273145245fee31a0f4feb81fb01"
MDSS_RESET_EDITS = {
    '\tstruct reset_control *reset;\n': '\tstruct reset_control *reset;\n\tint ret;\n',
    '\treset_control_assert(reset);\n': '''	ret = reset_control_assert(reset);
	if (ret) {
		dev_err_probe(dev, ret, "failed to assert mdss reset\\n");
		goto out;
	}
''',
    '\treset_control_deassert(reset);\n': '''	ret = reset_control_deassert(reset);
	if (ret)
		dev_err_probe(dev, ret, "failed to deassert mdss reset\\n");

out:
''',
    '\treset_control_put(reset);\n\n\treturn 0;\n':
        '\treset_control_put(reset);\n\n\treturn ret;\n',
}


def transform_mdss(original):
    if hashlib.sha256(original).hexdigest() != MDSS_SHA256:
        raise ValueError("MDSS source differs from the pinned Linux baseline")
    text = original.decode("utf-8")
    for anchor, replacement in MDSS_RESET_EDITS.items():
        if text.count(anchor) != 1:
            raise ValueError("unexpected MDSS reset anchors")
        text = text.replace(anchor, replacement)
    return text.encode("utf-8")


def transform_ubwc(original):
    if hashlib.sha256(original).hexdigest() != UBWC_SHA256:
        raise ValueError("UBWC source differs from the pinned Linux baseline")
    text = original.decode("utf-8")
    include = "#include <linux/soc/qcom/ubwc.h>\n"
    if text.count(include) != 1 or text.count(UBWC_CALL) != 1:
        raise ValueError("unexpected UBWC patch anchors")
    return text.replace(include, include + '#include "umi_abl_ubwc.h"\n').replace(
        UBWC_CALL, UBWC_CALL + UBWC_QUIRK).encode("utf-8")


def transform_dsi(original):
    if hashlib.sha256(original).hexdigest() != DSI_SHA256:
        raise ValueError("DSI source differs from the pinned Linux baseline")
    text = original.decode("utf-8")
    for anchor, addition in DSI_INSERTIONS.items():
        if text.count(anchor) != 1:
            raise ValueError("unexpected DSI diagnostic anchors")
        text = text.replace(anchor, anchor + addition)
    return text.encode("utf-8")


HOST_TRANSFORMS = {UBWC_PATH: transform_ubwc, DSI_PATH: transform_dsi,
                   MDSS_PATH: transform_mdss, CLOCK_PATH: transform_clock}


def stage_mdss(source):
    path = destination_dir(source, "drivers/gpu/drm/msm") / "msm_mdss.c"
    if path.is_symlink():
        raise ValueError("symlink MDSS target")
    current = original = path.read_bytes()
    for anchor, replacement in reversed(MDSS_RESET_EDITS.items()):
        original = original.replace(replacement.encode(), anchor.encode())
    patched = transform_mdss(original)
    if current not in (original, patched):
        raise ValueError("unexpected MDSS local modifications")
    path.write_bytes(patched)


def stage_dsi(source):
    target = destination_dir(source, "drivers/gpu/drm/msm/dsi") / "dsi_host.c"
    if target.is_symlink():
        raise ValueError("symlink DSI target")
    current = original = target.read_bytes()
    for addition in DSI_INSERTIONS.values():
        original = original.replace(addition.encode(), b"")
    patched = transform_dsi(original)
    if current not in (original, patched):
        raise ValueError("unexpected DSI local modifications")
    target.write_bytes(patched)


def stage_ubwc(source, inputs):
    target = destination_dir(source, "drivers/soc/qcom")
    path, header = target / "ubwc_config.c", target / "umi_abl_ubwc.h"
    if path.is_symlink() or header.is_symlink():
        raise ValueError("symlink UBWC target")
    current = path.read_bytes()
    # Accept only pristine source or exactly this reviewed, idempotent patch.
    original = current.replace(b'#include "umi_abl_ubwc.h"\n', b"").replace(UBWC_QUIRK.encode(), b"")
    patched = transform_ubwc(original)
    if current not in (original, patched):
        raise ValueError("unexpected UBWC local modifications")
    header.write_bytes((inputs / "umi_abl_ubwc.h").read_bytes())
    path.write_bytes(patched)


def stage(source, project, native=False):
    if source.is_symlink() or not source.is_dir():
        raise ValueError("invalid source tree")
    marker = source / ".miseed-source-revision"
    if marker.is_symlink() or marker.read_text().strip() != REVISION:
        raise ValueError("source is not the pinned Linux revision")
    target = destination_dir(source, "drivers/gpu/drm/panel")
    inputs = project / "kernel/display"
    stage_ubwc(source, inputs)
    stage_dsi(source)
    stage_mdss(source)
    stage_clock(source, native)
    for name in ("panel-xiaomi-umi-csot.c", "umi_csot_commands.h", "Kconfig", "Makefile", "umi-csot.Kconfig"):
        if (target / name).is_symlink():
            raise ValueError("symlink panel target: " + name)
    for name in ("panel-xiaomi-umi-csot.c", "umi_csot_commands.h"):
        (target / name).write_bytes((inputs / name).read_bytes())
    (target / "umi-csot.Kconfig").write_bytes((inputs / "Kconfig").read_bytes())
    for name, rule in (("Kconfig", 'source "drivers/gpu/drm/panel/umi-csot.Kconfig"'),
                       ("Makefile", "obj-$(CONFIG_DRM_PANEL_XIAOMI_UMI_CSOT) += panel-xiaomi-umi-csot.o")):
        path = target / name
        text = path.read_text()
        if rule not in text.splitlines():
            path.write_text(text + "\n" + rule + "\n")
    bindings = destination_dir(source, "Documentation/devicetree/bindings/display/panel")
    path = bindings / "xiaomi,umi-csot.yaml"
    if path.is_symlink():
        raise ValueError("symlink panel binding")
    path.write_bytes((inputs / "bindings/xiaomi,umi-csot.yaml").read_bytes())
    board = destination_dir(source, "arch/arm64/boot/dts/qcom")
    for name in ("umi-native-dsi.dtsi", "umi-csot.dts"):
        if (board / name).is_symlink():
            raise ValueError("symlink board target")
    (board / "umi-native-dsi.dtsi").write_bytes((inputs / "umi-native-dsi.dtsi").read_bytes())
    if native:
        path = board / "umi-csot.dts"
        text = path.read_text()
        rule = '#include "umi-native-dsi.dtsi"'
        if rule not in text:
            path.write_text(text + "\n" + rule + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--native", action="store_true")
    args = parser.parse_args()
    stage(args.source, Path(__file__).resolve().parents[1], args.native)

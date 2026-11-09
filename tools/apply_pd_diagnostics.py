"""Apply hash-locked PD PHY RX fixes and bounded TCPM diagnostics."""
# SPDX-License-Identifier: AGPL-3.0-only

import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REVISION = "f45ed76d6791cf72ff1265ee93511175bbbc2f18"
PDPHY = "drivers/usb/typec/tcpm/qcom/qcom_pmic_typec_pdphy.c"
TCPM = "drivers/usb/typec/tcpm/tcpm.c"
ORIGINAL = {
    PDPHY: "c154e44c1a9b6437ec379bf48505f75c50dcfcebd102f8057f4d666d774f473d",
    TCPM: "6837c4131c2c06b37818416d83778a1251833b23149995cb382bb94d01d06f42",
}
LOG_ENTRIES = 262144


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError("pinned PD diagnostic source anchor changed")
    return text.replace(old, new, 1)


def transform(name, text, receive):
    if name == TCPM:
        return replace_once(text, "#define LOG_BUFFER_ENTRIES\t1024",
                            f"#define LOG_BUFFER_ENTRIES\t{LOG_ENTRIES}")
    start = text.index("static void qcom_pmic_typec_pdphy_pd_receive(")
    end = text.index("\nstatic irqreturn_t", start)
    text = text[:start] + receive.rstrip() + "\n" + text[end:]
    text = replace_once(text,
        "case PMIC_PDPHY_SIG_RX_IRQ:\n\t\tschedule_work",
        'case PMIC_PDPHY_SIG_RX_IRQ:\n\t\tdev_info_ratelimited(dev, "miseed-pdphy rx_irq_signal\\n");\n\t\tschedule_work')
    text = replace_once(text,
        '\tdev_vdbg(dev, "pd_transmit_signal: type %d negotiate_rev %d send %d\\n",',
        '\tdev_info_ratelimited(dev, "miseed-pdphy tx_reset_signal type=%d revision=%u rc=%d\\n",\n\t\t\t     type, negotiated_rev, ret);\n\tdev_vdbg(dev, "pd_transmit_signal: type %d negotiate_rev %d send %d\\n",')
    return text


def regular_path(root, relative):
    current = root
    for part in Path(relative).parts:
        current = current / part
        if current.is_symlink():
            raise ValueError(f"refusing symlink: {relative}")
    if not current.is_file():
        raise ValueError(f"required source file missing: {relative}")
    return current


def stage(source, receive_path=ROOT / "kernel/power/pmic_pdphy_receive.inc"):
    source = Path(source)
    if source.is_symlink() or not source.is_dir():
        raise ValueError("source must be a regular directory")
    if regular_path(source, ".miseed-source-revision").read_text().strip() != REVISION:
        raise ValueError("source is not the pinned Linux revision")
    receive = receive_path.read_text(encoding="utf-8")
    marker = source / ".miseed-pd-diagnostics.json"
    if marker.is_symlink():
        raise ValueError("refusing diagnostic manifest symlink")
    if marker.exists():
        saved = json.loads(marker.read_text())
        if not isinstance(saved, dict):
            raise ValueError("diagnostic manifest must be an object")
        if saved.get("revision") != REVISION or saved.get("receive_sha256") != digest(receive.encode()) or saved.get("applier_sha256") != digest(Path(__file__).read_bytes()):
            raise ValueError("diagnostic inputs changed; use a fresh source tree")
        if saved.get("original") != ORIGINAL or set(saved.get("outputs", {})) != set(ORIGINAL):
            raise ValueError("diagnostic manifest source set changed")
        for name, expected in saved["outputs"].items():
            if digest(regular_path(source, name).read_bytes()) != expected:
                raise ValueError("previously patched PD source changed")
        return saved
    # Check every original before writing either file.
    patches = {}
    for name, expected in ORIGINAL.items():
        data = regular_path(source, name).read_bytes()
        if digest(data) != expected:
            raise ValueError(f"pinned source SHA-256 mismatch: {name}")
        patches[name] = transform(name, data.decode("utf-8"), receive).encode("utf-8")
    saved = dict(revision=REVISION, receive_sha256=digest(receive.encode()),
                 applier_sha256=digest(Path(__file__).read_bytes()),
                 original=ORIGINAL, outputs={n: digest(b) for n, b in patches.items()},
                 tcpm_log_entries=LOG_ENTRIES)
    for name, data in patches.items():
        regular_path(source, name).write_bytes(data)
    marker.write_text(json.dumps(saved, indent=2) + "\n", encoding="utf-8", newline="\n")
    return saved


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    try:
        stage(args.source)
    except (OSError, ValueError) as error:
        parser.exit(1, f"PD diagnostic staging failed: {error}\n")
    print(f"Staged guarded PD RX and {LOG_ENTRIES}-entry TCPM diagnostics")


if __name__ == "__main__":
    main()

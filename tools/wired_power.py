"""Inspect/control Umi wired charging through Linux power_supply (no Android ABI)."""
# SPDX-License-Identifier: AGPL-3.0-only

import argparse
import json
from pathlib import Path

ATTRIBUTES = ("type", "model_name", "status", "health", "online", "present",
              "capacity", "voltage_now", "current_now", "temp",
              "charge_full_design", "voltage_min_design", "voltage_max_design",
              "input_current_limit", "charge_behaviour", "usb_type")


def discover(root):
    """Only the known wired chargers and gauge; never select a wireless supply."""
    found = {}
    if not root.is_dir():
        raise ValueError("power_supply class is missing")
    for path in root.iterdir():
        if path.name in ("qcom-battery", "pm8150b-charger"):
            role = "battery" if path.name == "qcom-battery" else "switch_charger"
        else:
            try:
                model = (path / "model_name").read_text().strip()
            except OSError:
                continue
            if model != "BQ25970":
                continue
            role = "pump"
        if role in found:
            raise ValueError(f"ambiguous {role} supplies")
        found[role] = path
    return found


def snapshot(supplies):
    result = {}
    for role, path in supplies.items():
        values = {"name": path.name}
        for attr in ATTRIBUTES:
            try:
                values[attr] = (path / attr).read_text().strip()
            except FileNotFoundError:
                continue  # Unsupported attributes are not fabricated as zero.
            except OSError as exc:
                values[attr] = {"error": exc.errno}
        result[role] = values
    return result


def set_behaviour(supplies, action):
    """Inhibit pump before SMB5; enable SMB5 before permitting automatic PPS.

    Always attempt both inhibits, even when pump stop fails. The driver keeps
    ownership on failure and may reject SMB5 access; report every failed write.
    """
    if action not in ("inhibit-charge", "auto"):
        raise ValueError("unsupported charge behaviour")
    missing = {"switch_charger", "pump"} - supplies.keys()
    if missing:
        raise ValueError("missing wired supply: " + ", ".join(sorted(missing)))
    order = ("pump", "switch_charger") if action == "inhibit-charge" else ("switch_charger", "pump")
    errors = {}
    for role in order:
        try:
            (supplies[role] / "charge_behaviour").write_text(action + "\n")
        except OSError as exc:
            errors[role] = exc.errno
            if action == "auto":
                break
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sysfs-root", type=Path, default=Path("/sys/class/power_supply"))
    parser.add_argument("--action", choices=("status", "inhibit-charge", "auto"), default="status")
    args = parser.parse_args()
    try:
        supplies = discover(args.sysfs_root)
        missing = sorted({"battery", "switch_charger", "pump"} - supplies.keys())
        errors = {} if args.action == "status" else set_behaviour(supplies, args.action)
        print(json.dumps({"scope": "wired", "supplies": snapshot(supplies),
                          "missing": missing, "control_errors": errors}, indent=2))
        return int(bool(missing or errors))
    except (OSError, ValueError) as exc:
        parser.exit(1, f"wired power: {exc}\n")


if __name__ == "__main__":
    raise SystemExit(main())

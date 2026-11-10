"""Synthetic standard Linux power_supply consumers; no device access."""
# SPDX-License-Identifier: AGPL-3.0-only
import errno
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import wired_power


class WiredPowerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ("qcom-battery", "pm8150b-charger", "pump0", "wireless"):
            (self.root / name).mkdir()
        (self.root / "pump0/model_name").write_text("BQ25970\n")
        (self.root / "wireless/model_name").write_text("IDTP9221\n")
        (self.root / "qcom-battery/capacity").write_text("73\n")
        for name in ("pm8150b-charger", "pump0"):
            (self.root / name / "charge_behaviour").write_text("[auto] inhibit-charge\n")

    def test_read_only_discovery_and_optional_properties(self):
        supplies = wired_power.discover(self.root)
        self.assertEqual(set(supplies), {"battery", "switch_charger", "pump"})
        values = wired_power.snapshot(supplies)
        self.assertEqual(values["battery"]["capacity"], "73")
        self.assertNotIn("charge_counter", values["battery"])
        self.assertEqual(values["pump"]["charge_behaviour"], "[auto] inhibit-charge")

    def test_missing_and_ambiguous_never_write(self):
        supplies = wired_power.discover(self.root)
        with self.assertRaisesRegex(ValueError, "missing"):
            wired_power.set_behaviour({"pump": supplies["pump"]}, "auto")
        (self.root / "clone").mkdir()
        (self.root / "clone/model_name").write_text("BQ25970\n")
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            wired_power.discover(self.root)

    def test_inhibit_order_and_failure_still_attempts_switch_charger(self):
        supplies = wired_power.discover(self.root)
        calls = []
        def write(path, value):
            calls.append((path.parent.name, value))
            if path.parent.name == "pump0":
                raise OSError(errno.EIO, "injected pump stop failure")
        with patch.object(Path, "write_text", write):
            errors = wired_power.set_behaviour(supplies, "inhibit-charge")
        self.assertEqual(calls, [("pump0", "inhibit-charge\n"),
                                 ("pm8150b-charger", "inhibit-charge\n")])
        self.assertEqual(errors, {"pump": errno.EIO})

    def test_auto_failure_does_not_start_pump(self):
        supplies = wired_power.discover(self.root)
        calls = []
        def write(path, value):
            calls.append(path.parent.name)
            raise OSError(errno.EBUSY, "ownership retained")
        with patch.object(Path, "write_text", write):
            errors = wired_power.set_behaviour(supplies, "auto")
        self.assertEqual(calls, ["pm8150b-charger"])
        self.assertEqual(errors, {"switch_charger": errno.EBUSY})

    def test_read_failure_is_reported_not_zero(self):
        supplies = wired_power.discover(self.root)
        original = Path.read_text
        def read(path, *args, **kwargs):
            if path.name == "voltage_now":
                raise OSError(errno.EIO, "missing telemetry")
            return original(path, *args, **kwargs)
        with patch.object(Path, "read_text", read):
            values = wired_power.snapshot(supplies)
        self.assertEqual(values["battery"]["voltage_now"], {"error": errno.EIO})


if __name__ == "__main__":
    unittest.main()

"""Offline safety boundaries for the unqualified FG/SMB5 compile port."""

# SPDX-License-Identifier: AGPL-3.0-only

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import apply_power_port


ROOT = Path(__file__).resolve().parents[1]


from power_test_support import PowerPortFixture, power_source


class PowerPortTests(PowerPortFixture):
    def test_restage_replaces_stale_generated_driver_without_duplicate_rules(self):
        self.stage()
        for name in apply_power_port.POWER_FILES:
            self.assertEqual((self.target / name).read_bytes(),
                             (ROOT / "kernel/power" / name).read_bytes())
        rules = (self.target / "Makefile").read_text().splitlines()
        for rule in apply_power_port.RULES:
            self.assertEqual(rules.count(rule), 1)
        snapshot = {p.name: p.read_bytes() for p in self.target.iterdir()}
        rpmhpd_snapshot = self.rpmhpd_kconfig.read_bytes()
        self.assertIn(b"\tselect PM_OPP\n", rpmhpd_snapshot)
        (self.target / "qcom_fg_core.c").write_text("stale source\n")
        self.stage()
        self.assertEqual(snapshot, {p.name: p.read_bytes() for p in self.target.iterdir()})
        self.assertEqual(rpmhpd_snapshot, self.rpmhpd_kconfig.read_bytes())


    def test_wrong_revision_does_not_mutate_tree(self):
        (self.source / ".miseed-source-revision").write_text("0" * 40)
        with self.assertRaisesRegex(ValueError, "pinned Linux"):
            self.stage()
        self.assertFalse((self.target / "qcom_fg_core.c").exists())


    @unittest.skipIf(os.name == "nt", "POSIX FIFO fixture")
    def test_nonregular_revision_marker_is_rejected_before_read(self):
        marker = self.source / ".miseed-source-revision"
        marker.unlink()
        os.mkfifo(marker)
        with self.assertRaisesRegex(ValueError, "regular file"):
            self.stage()


    @unittest.skipIf(os.name == "nt", "POSIX symlink fixture")
    def test_symlinked_power_destination_directory_is_rejected(self):
        external = self.source / "external"
        external.mkdir()
        (external / "Makefile").write_text("keep\n")
        shutil.rmtree(self.target)
        self.target.symlink_to(external, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "symlink directory"):
            self.stage()
        self.assertEqual((external / "Makefile").read_text(), "keep\n")


    @unittest.skipIf(os.name == "nt", "POSIX symlink fixture")
    def test_symlinked_board_destination_directory_is_rejected(self):
        dts_root = self.source / "arch/arm64/boot/dts"
        dts_root.mkdir(parents=True)
        external = self.source / "external-board"
        external.mkdir()
        (dts_root / "qcom").symlink_to(external, target_is_directory=True)
        with self.assertRaises(SystemExit):
            with patch("sys.argv", ["apply_power_port", "--source", str(self.source)]):
                apply_power_port.main()
        self.assertEqual(list(external.iterdir()), [])


    @unittest.skipIf(os.name == "nt", "POSIX symlink fixture")
    def test_symlink_does_not_overwrite_external_file(self):
        external = self.source / "external.c"
        external.write_text("preserve\n")
        (self.target / "qcom_fg_core.c").symlink_to(external)
        with self.assertRaisesRegex(ValueError, "symlink"):
            self.stage()
        self.assertEqual(external.read_text(), "preserve\n")


    @unittest.skipUnless(os.name == "posix" and shutil.which("bash"), "POSIX Bash required")
    def test_build_rejects_modular_or_missing_runtime_power_driver(self):
        script = (ROOT / "tools/build_kernel_l2.sh").read_text()
        start = script.index("for option in BATTERY_MISEED_QCOM_FG")
        stop = script.index("for option in MEMFD_CREATE", start)
        check = 'set -eu\nbuild_tree="$1"\nfail() { exit 42; }\n' + script[start:stop]
        for state, expected in (("y", 0), ("m", 42), ("n", 42)):
            with self.subTest(state=state):
                (self.source / ".config").write_text(
                    f"CONFIG_BATTERY_MISEED_QCOM_FG={state}\n"
                    f"CONFIG_CHARGER_MISEED_PM8150B={state}\n"
                    "CONFIG_MODULES=y\nCONFIG_POWER_SUPPLY=y\nCONFIG_IIO=y\nCONFIG_QCOM_SPMI_ADC5=y\n"
                    "CONFIG_DRM=y\nCONFIG_DRM_SIMPLEDRM=y\nCONFIG_DRM_FBDEV_EMULATION=y\n"
                    "CONFIG_DRM_CLIENT_DEFAULT_FBDEV=y\nCONFIG_FB_CORE=y\nCONFIG_FRAMEBUFFER_CONSOLE=y\n"
                    "CONFIG_GPIOLIB=y\nCONFIG_NET=y\nCONFIG_NFC=y\nCONFIG_NFC_NCI=y\nCONFIG_NFC_SN100X=y\n"
                    "CONFIG_CHARGER_MISEED_UMI_PPS=y\nCONFIG_I2C_QCOM_GENI=y\n"
                    "CONFIG_TYPEC_TCPM=y\nCONFIG_TYPEC_QCOM_PMIC=y\n"
                    "CONFIG_REGULATOR_QCOM_USB_VBUS=y\nCONFIG_USB=y\n")
                result = subprocess.run(["bash", "-c", check, "fixture", str(self.source)])
                self.assertEqual(result.returncode, expected)


if __name__ == "__main__":
    unittest.main()

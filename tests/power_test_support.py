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


from tools.source_layout import POWER_GROUPS


def power_source(group):
    return "\n".join((ROOT / "kernel/power" / (name + ".c")).read_text()
                     for name in POWER_GROUPS[group]) + "\n" + (ROOT / "kernel/power/qcom_pm8150b_regs.h").read_text()


class PowerPortFixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name)
        (self.source / ".miseed-source-revision").write_text(apply_power_port.REVISION)
        self.target = self.source / "drivers/power/supply"
        self.target.mkdir(parents=True)
        for name in ("Makefile", "Kconfig"):
            (self.target / name).write_text("# fixture\n")
        self.rpmhpd_kconfig = self.source / "drivers/pmdomain/qcom/Kconfig"
        self.rpmhpd_kconfig.parent.mkdir(parents=True)
        self.rpmhpd_kconfig.write_text(
            'config QCOM_RPMHPD\n'
            '\ttristate "Qualcomm RPMh Power domain driver"\n'
            '\tdepends on QCOM_RPMH && QCOM_COMMAND_DB\n'
        )


    def stage(self):
        apply_power_port.stage(self.source, ROOT / "kernel/power")

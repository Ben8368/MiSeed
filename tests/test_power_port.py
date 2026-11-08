"""Offline safety boundaries for the unqualified FG/SMB5 compile port."""

# SPDX-License-Identifier: AGPL-3.0-only

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools import apply_power_port


ROOT = Path(__file__).resolve().parents[1]


class PowerPortTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name)
        (self.source / ".miseed-source-revision").write_text(apply_power_port.REVISION)
        self.target = self.source / "drivers/power/supply"
        self.target.mkdir(parents=True)
        for name in ("Makefile", "Kconfig"):
            (self.target / name).write_text("# fixture\n")

    def stage(self):
        apply_power_port.stage(self.source, ROOT / "kernel/power")

    def test_restage_replaces_stale_generated_driver_without_duplicate_rules(self):
        self.stage()
        snapshot = {p.name: p.read_bytes() for p in self.target.iterdir()}
        (self.target / "qcom_fg.c").write_text("stale source\n")
        self.stage()
        self.assertEqual(snapshot, {p.name: p.read_bytes() for p in self.target.iterdir()})

    def test_wrong_revision_does_not_mutate_tree(self):
        (self.source / ".miseed-source-revision").write_text("0" * 40)
        with self.assertRaisesRegex(ValueError, "pinned Linux"):
            self.stage()
        self.assertFalse((self.target / "qcom_fg.c").exists())

    @unittest.skipIf(os.name == "nt", "POSIX symlink fixture")
    def test_symlink_does_not_overwrite_external_file(self):
        external = self.source / "external.c"
        external.write_text("preserve\n")
        (self.target / "qcom_fg.c").symlink_to(external)
        with self.assertRaisesRegex(ValueError, "symlink"):
            self.stage()
        self.assertEqual(external.read_text(), "preserve\n")

    @unittest.skipUnless(shutil.which("bash"), "Bash required")
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
                    "CONFIG_MODULES=y\nCONFIG_POWER_SUPPLY=y\nCONFIG_IIO=y\nCONFIG_QCOM_SPMI_ADC5=y\n")
                result = subprocess.run(["bash", "-c", check, "fixture", str(self.source)])
                self.assertEqual(result.returncode, expected)

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_standard_behaviour_enum_cannot_reverse_enable_or_accept_force_discharge(self):
        source = (ROOT / "kernel/power/qcom_pm8150b_charger.c").read_text()
        functions = []
        for name in ("smb5_get_prop_charging_enabled", "smb5_set_prop_charging_enabled"):
            start = source.index("static int " + name + "(")
            stop = source.index("\n}\n", start) + 3
            functions.append(source[start:stop])
        harness = r'''
#include <assert.h>
#include <errno.h>
#define CHARGING_ENABLE_CMD 0x42
#define CHARGING_ENABLE_CMD_BIT 1
enum { POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO = 0,
       POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE = 1,
       POWER_SUPPLY_CHARGE_BEHAVIOUR_FORCE_DISCHARGE = 2 };
struct smb5_chip { void *regmap; void *dev; unsigned int base; };
static unsigned int written, reads, writes, command;
static int failure;
#define dev_err(...) ((void)0)
static int regmap_read(void *map, unsigned int reg, unsigned int *v)
{ (void)map; assert(reg == 0x1042); ++reads; *v = command; return failure; }
static int regmap_update_bits(void *map, unsigned int reg,
                              unsigned int mask, unsigned int v)
{ (void)map; assert(reg == 0x1042); assert(mask == 1); ++writes; written = v; return failure; }
'''
        harness += "\n".join(functions) + r'''
int main(void) {
    struct smb5_chip chip = { .base = 0x1000 };
    int result = -1;
    assert(!smb5_set_prop_charging_enabled(&chip, POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO));
    assert(writes == 1 && written == 1);
    assert(!smb5_set_prop_charging_enabled(&chip, POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE));
    assert(writes == 2 && written == 0);
    assert(smb5_set_prop_charging_enabled(&chip, POWER_SUPPLY_CHARGE_BEHAVIOUR_FORCE_DISCHARGE) == -EINVAL);
    assert(smb5_set_prop_charging_enabled(&chip, (unsigned int)-1) == -EINVAL);
    assert(writes == 2);
    command = 0x81;
    assert(!smb5_get_prop_charging_enabled(&chip, &result) && result == 0);
    command = 0x80;
    assert(!smb5_get_prop_charging_enabled(&chip, &result) && result == 1);
    failure = -EIO;
    result = 77;
    assert(smb5_get_prop_charging_enabled(&chip, &result) == -EIO && result == 77);
    assert(smb5_set_prop_charging_enabled(&chip, 0) == -EIO);
    assert(reads == 3);
    return 0;
}
'''
        entry = self.source / "behaviour.c"
        binary = self.source / "behaviour"
        entry.write_text(harness)
        subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", str(entry), "-o", str(binary)],
                       check=True, capture_output=True)
        subprocess.run([str(binary)], check=True, timeout=10)

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_test_mode_bypasses_soc_only_and_preserves_all_safety_boundaries(self):
        harness = r'''
#include <assert.h>
#include "umi_charge_policy.h"
#define D(on,p,h,t,v,s,old,test) umi_charge_policy(on,p,h,t,v,s,4400000,old,test)
int main(void) {
 assert(D(1,1,1,300,4100000,65,0,0) == 1);
 assert(D(1,1,1,300,4100000,70,1,0) == 0);
 assert(D(1,1,1,300,4100000,68,0,0) == 0);
 assert(D(1,1,1,300,4100000,68,1,0) == 1);
 assert(D(1,1,1,300,4100000,78,0,1) == 1);
 assert(D(1,1,1,300,4100000,100,0,1) == 0);
 assert(D(0,1,1,300,4100000,60,1,1) == 0);
 assert(D(1,0,1,300,4100000,60,1,1) == 0);
 assert(D(1,1,0,300,4100000,60,1,1) == 0);
 assert(D(1,1,1,150,4100000,60,1,1) == 0);
 assert(D(1,1,1,151,4100000,60,1,1) == 1);
 assert(D(1,1,1,450,4100000,60,1,1) == 1);
 assert(D(1,1,1,451,4100000,60,1,1) == 0);
 assert(D(1,1,1,-10000,4100000,60,1,1) == 0);
 assert(D(1,1,1,300,4400000,60,1,1) == 0);
 assert(D(1,1,1,300,-1,60,1,1) == 0);
 assert(D(1,1,1,300,4100000,-1,1,1) == 0);
 assert(D(1,1,1,300,4100000,101,1,1) == 0);
 assert(umi_charge_policy(1,1,1,300,4100000,60,4480000,1,1) == 0);
 return 0;
}
'''
        entry = self.source / "policy.c"
        binary = self.source / "policy"
        entry.write_text(harness)
        subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "kernel/power"),
                        str(entry), "-o", str(binary)], check=True, capture_output=True)
        subprocess.run([str(binary)], check=True, timeout=10)

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_pm8150b_voltage_encoding_preserves_lower_inherited_limit_and_errors(self):
        source = (ROOT / "kernel/power/qcom_pm8150b_charger.c").read_text()
        start = source.index("static int smb5_init_hw(")
        stop = source.index("\n}\n", start) + 3
        harness = r'''
#include <assert.h>
#include <errno.h>
#include "umi_charge_policy.h"
#define BIT(n) (1U << (n))
#define min(a,b) ((a) < (b) ? (a) : (b))
#define FLOAT_VOLTAGE_CFG 0x70
#define FAST_CHARGE_CURRENT_CFG 0x61
#define CURRENT_SCALE_FACTOR 50000
#define SNARL_BARK_BITE_WD_CFG 0x653
#define BITE_WDOG_DISABLE_CHARGING_CFG_BIT BIT(7)
#define WD_CFG 0x651
#define WDOG_TIMER_EN_BIT BIT(0)
#define WDOG_TIMER_EN_ON_PLUGIN_BIT BIT(1)
#define BARK_WDOG_INT_EN_BIT BIT(6)
struct smb5_chip { void *regmap; unsigned int base; int max_fv_uv; };
static unsigned int registers[0x2000], writes;
static int fail_read;
static int regmap_read(void *m, unsigned int a, unsigned int *v)
{ (void)m; if (fail_read) return -EIO; *v = registers[a]; return 0; }
static int regmap_write(void *m, unsigned int a, unsigned int v)
{ (void)m; assert(a != 0x1042); registers[a] = v; writes++; return 0; }
static int regmap_update_bits(void *m, unsigned int a, unsigned int mask, unsigned int v)
{ return regmap_write(m, a, (registers[a] & ~mask) | (v & mask)); }
static int smb5_set_current_limit(struct smb5_chip *c, unsigned int v)
{ return regmap_write(c->regmap, c->base + 0x370, v / CURRENT_SCALE_FACTOR); }
'''
        harness += source[start:stop] + r'''
int main(void) {
 struct smb5_chip c = { .base = 0x1000 };
 registers[0x1070] = 78;
 assert(!smb5_init_hw(&c));
 assert(c.max_fv_uv == 4380000 && registers[0x1070] == 78);
 assert(registers[0x1061] == 10 && registers[0x1370] == 10);
 assert(registers[0x1653] & BIT(7));
 assert((registers[0x1651] & (BIT(0)|BIT(1)|BIT(6))) == (BIT(0)|BIT(1)|BIT(6)));
 assert(registers[0x1670] & BIT(0));
 registers[0x1070] = 88;
 assert(!smb5_init_hw(&c));
 assert(c.max_fv_uv == 4450000 && registers[0x1070] == 85);
 registers[0x1070] = 0; writes = 0;
 assert(smb5_init_hw(&c) == -ERANGE && writes == 0);
 fail_read = 1;
 assert(smb5_init_hw(&c) == -EIO && writes == 0);
 return 0;
}
'''
        entry = self.source / "encoding.c"
        binary = self.source / "encoding"
        entry.write_text(harness)
        subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "kernel/power"),
                        str(entry), "-o", str(binary)], check=True, capture_output=True)
        subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()

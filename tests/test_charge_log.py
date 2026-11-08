"""Offline power-observer errors and USB export framing; no device access."""

# SPDX-License-Identifier: AGPL-3.0-only

import io
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools import collect_charge_log as collector
from tools import apply_power_observer as applier


ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which("cc") or shutil.which("gcc")


@unittest.skipUnless(os.name == "posix", "POSIX pipes required")
class ExportTests(unittest.TestCase):
    def receive(self, data):
        read_fd, write_fd = os.pipe()
        try:
            os.write(write_fd, data)
            os.close(write_fd)
            write_fd = None
            stream = io.BytesIO()
            bounds = collector.receive(read_fd, stream, 0.5)
            return bounds, stream.getvalue()
        finally:
            os.close(read_fd)
            if write_fd is not None:
                os.close(write_fd)

    def test_complete_export_ignores_banner_and_preserves_overwrite_count(self):
        snapshot = (b"CHARGE_LOG_BEGIN boot_id=test first=7 next=9 count=2 overwritten=7 ram_only=1\r\n"
                    b"seq=7 boot_ms=100 KERNEL charger inserted\r\n"
                    b"seq=8 boot_ms=200 KERNEL charger removed\r\n"
                    b"CHARGE_LOG_END next=9\r\n")
        bounds, data = self.receive(b"MiSeed diagnostic\r\n" + snapshot + b"miseed> ")
        self.assertEqual(bounds, (7, 9, 2, 7))
        self.assertEqual(data, snapshot.replace(b"\r\n", b"\n"))

    def test_empty_export_is_valid(self):
        bounds, _ = self.receive(b"CHARGE_LOG_BEGIN boot_id=test first=0 next=0 count=0 overwritten=0 ram_only=1\n"
                                 b"CHARGE_LOG_END next=0\n")
        self.assertEqual(bounds, (0, 0, 0, 0))

    def test_gaps_duplicates_wrong_footer_disconnect_and_unavailable_fail(self):
        begin = b"CHARGE_LOG_BEGIN boot_id=test first=0 next=2 count=2 overwritten=0 ram_only=1\n"
        cases = [begin + b"seq=1 boot_ms=0 SAMPLE gap\nCHARGE_LOG_END next=2\n",
                 begin + b"seq=0 boot_ms=0 SAMPLE a\nseq=0 boot_ms=1 SAMPLE duplicate\n",
                 begin + b"seq=0 boot_ms=0 SAMPLE a\nCHARGE_LOG_END next=1\n",
                 begin + b"seq=0 boot_ms=0 SAMPLE incomplete\n",
                 b"CHARGE_LOG_UNAVAILABLE recorder_not_started\n"]
        for data in cases:
            with self.subTest(data=data), self.assertRaises(collector.ExportError):
                self.receive(data)

    def test_output_is_ignored_local_and_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(collector, "ROOT", Path(directory).resolve()):
            root = Path(directory).resolve()
            output = root / "local/session.log"
            self.assertEqual(collector.output_path(output), output)
            for bad in (root / "tracked.log", root / "local/session.txt"):
                with self.assertRaises(collector.ExportError):
                    collector.output_path(bad)
            output.parent.mkdir()
            output.write_text("previous")
            with self.assertRaises(collector.ExportError):
                collector.output_path(output)
            self.assertEqual(output.read_text(), "previous")


HARNESS = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define BIT(n) (1U << (n))
#define GENMASK(high, low) (((1U << ((high) + 1)) - 1) & ~((1U << (low)) - 1))
@OBSERVER_CONSTANTS@
struct power_observer { void *dev, *regmap; };
static unsigned int values[4];
static int reads, fault;
static char output[1024];
static int regmap_read(void *regmap, unsigned int reg, unsigned int *value) {
    static const unsigned int allowed[] = { 0x1310, 0x1006, 0x1007, 0x1042 };
    int index = reads++;
    assert(index < 4 && reg == allowed[index]);
    if (index == fault) return -5;
    *value = values[index];
    return 0;
}
static void dev_info(void *dev, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(output, sizeof(output), format, args);
    va_end(args);
}
@SAMPLE@
int main(void) {
    struct power_observer observer = { 0 };
    static const char * const phases[] = {
        "inhibit", "trickle", "precharge", "fullon", "taper", "terminate", "pause", "disabled"
    };
    for (int state = 0; state < 8; state++) {
        reads = 0; fault = -1;
        values[0] = 0x10; values[1] = state; values[2] = 0x22; values[3] = 1;
        power_observer_sample(&observer);
        assert(reads == 4 && strstr(output, phases[state]));
        assert(strstr(output, "usb_present=1") && strstr(output, "chg_enable=1"));
        assert(strstr(output, "chgr2=0x22") && strstr(output, "control=read-only"));
    }
    for (int index = 0; index < 4; index++) {
        reads = 0; fault = index;
        power_observer_sample(&observer);
        assert(reads == index + 1 && strstr(output, "read_error"));
        assert(!strstr(output, "hw_phase=") && !strstr(output, "usb_present="));
    }
    reads = 0; fault = -1; values[0] = values[3] = 0;
    power_observer_sample(&observer);
    assert(strstr(output, "usb_present=0") && strstr(output, "chg_enable=0"));
    return 0;
}
'''


class ObserverTests(unittest.TestCase):
    def test_applier_checks_revision_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = ["apply_power_observer", "--source", str(root)]
            with patch("sys.argv", args), self.assertRaises(SystemExit):
                applier.main()
            (root / ".miseed-source-revision").write_text(applier.REVISION)
            (root / "drivers/mfd").mkdir(parents=True)
            makefile = root / "drivers/mfd/Makefile"
            makefile.write_text("# fixture\n")
            with patch("sys.argv", args):
                applier.main()
                applier.main()
            self.assertEqual(makefile.read_text().count(applier.RULE), 1)
            self.assertEqual((root / "drivers/mfd/miseed_power_observer.c").read_bytes(),
                             (ROOT / "kernel/power_observer.c").read_bytes())

    @unittest.skipUnless(CC, "C compiler required")
    def test_status_reads_and_all_error_branches(self):
        source = (ROOT / "kernel/power_observer.c").read_text()
        # This diagnostic must never grow a charge-control side effect.
        self.assertNotRegex(source, r"\b(?:regmap_(?:write|bulk_write|update_bits|multi_reg_write)|request_irq|devm_request_threaded_irq)\s*\(")
        start = source.index("static void power_observer_sample(")
        end = source.index("\n}\n", start) + 3
        constants = "\n".join(line for line in source.splitlines()
                                if line.startswith("#define OBS_"))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "observer.c"
            path.write_text(HARNESS.replace("@OBSERVER_CONSTANTS@", constants)
                            .replace("@SAMPLE@", source[start:end]))
            binary = Path(directory) / "observer"
            result = subprocess.run([CC, "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                                     "-Wno-sign-compare", str(path), "-o", str(binary)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([binary], check=True, timeout=5)


if __name__ == "__main__":
    unittest.main()

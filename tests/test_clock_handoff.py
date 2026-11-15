"""Idle orphan clocks must not power an unconfigured parent during adoption."""
# SPDX-License-Identifier: GPL-2.0-only
import hashlib
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from tools import apply_clock_handoff as clock


class ClockHandoffTests(unittest.TestCase):
    def test_stage_is_exact_idempotent_and_fail_closed(self):
        original = ("prefix\n" + clock.CLOCK_BEFORE + "suffix\n").encode()
        with tempfile.TemporaryDirectory() as tmp, patch.object(
                clock, "CLOCK_SHA256", hashlib.sha256(original).hexdigest()):
            root = Path(tmp)
            path = root / clock.CLOCK_PATH
            path.parent.mkdir(parents=True)
            path.write_bytes(original)
            clock.stage_clock(root)
            expected = clock.transform_clock(original)
            self.assertEqual(path.read_bytes(), expected)
            clock.stage_clock(root)
            self.assertEqual(path.read_bytes(), expected)
            clock.stage_clock(root, native=False)
            self.assertEqual(path.read_bytes(), original)
            clock.stage_clock(root)
            path.write_bytes(expected + b"unknown edit\n")
            with self.assertRaisesRegex(ValueError, "baseline"):
                clock.stage_clock(root)
            self.assertEqual(path.read_bytes(), expected + b"unknown edit\n")
            if shutil.which("cc"):
                path.unlink()
                victim = root / "victim"
                victim.write_bytes(original)
                try:
                    path.symlink_to(victim)
                except OSError as error:
                    if getattr(error, "winerror", None) == 1314:
                        self.skipTest("symlink privilege required; covered in WSL")
                    raise
                with self.assertRaisesRegex(ValueError, "symlink"):
                    clock.stage_clock(root)
                self.assertEqual(victim.read_bytes(), original)

    def test_unique_anchor_and_source_identity(self):
        for original in (b"", (clock.CLOCK_BEFORE * 2).encode()):
            with patch.object(clock, "CLOCK_SHA256", hashlib.sha256(original).hexdigest()):
                with self.assertRaisesRegex(ValueError, "anchors"):
                    clock.transform_clock(original)
        with self.assertRaisesRegex(ValueError, "baseline"):
            clock.transform_clock(clock.CLOCK_BEFORE.encode())

    @unittest.skipUnless(shutil.which("cc"), "C compiler required")
    def test_idle_adoption_and_prepared_reference_migration(self):
        # Synthetic clock transport fails if an idle PLL is started. Prepared
        # clocks have a ready provider and must retain reference migration.
        harness = r'''#include <assert.h>
#include <stddef.h>
struct clk_core { int prepare_count; struct clk_core *parent; };
static int locked, starts, stops, before_calls, after_calls;
static unsigned long clk_enable_lock(void) { assert(!locked); locked = 1; return 42; }
static void clk_enable_unlock(unsigned long flags) { assert(locked && flags == 42); locked = 0; }
static void clk_reparent(struct clk_core *c, struct clk_core *p) { assert(locked); c->parent = p; }
static void __clk_set_parent_before(struct clk_core *c, struct clk_core *p) {
    assert(c->prepare_count > 0); before_calls++;
    starts += 2; p->prepare_count += 2;
    unsigned long flags = clk_enable_lock(); clk_reparent(c, p); clk_enable_unlock(flags);
}
static void __clk_set_parent_after(struct clk_core *c, struct clk_core *p, void *old) {
    assert(c->prepare_count > 0 && old == NULL); after_calls++;
    stops++; p->prepare_count--;
}
static void adopt(struct clk_core *orphan, struct clk_core *parent) {
    if (parent) {
/* ADOPTION */
    }
}
int main(void) {
    struct clk_core idle = {0}, parent = {0};
    adopt(&idle, &parent);
    assert(idle.parent == &parent && !locked);
    assert(!starts && !stops && !parent.prepare_count);
    assert(!before_calls && !after_calls);
    struct clk_core missing = {0};
    adopt(&missing, NULL);
    assert(!missing.parent && !starts);
    struct clk_core prepared = {.prepare_count = 1};
    adopt(&prepared, &parent);
    assert(prepared.parent == &parent && prepared.prepare_count == 1);
    assert(starts == 2 && stops == 1 && parent.prepare_count == 1);
    assert(before_calls == 1 && after_calls == 1 && !locked);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source, binary = Path(tmp) / "clock.c", Path(tmp) / "clock"
            source.write_text(harness.replace("/* ADOPTION */", clock.CLOCK_AFTER))
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", str(binary)], check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, timeout=10)

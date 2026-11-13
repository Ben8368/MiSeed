"""Stage the pinned CCF orphan-adoption fix for native-display diagnostics."""
# SPDX-License-Identifier: GPL-2.0-only
# CCF fragment derived from Linux drivers/clk/clk.c; original notices retained.
import hashlib

CLOCK_PATH = "drivers/clk/clk.c"
CLOCK_SHA256 = "a30805dc12b79b20f41dfaf20ce18aa04aa6afb332317b7c1310df3d2b2cd235"
CLOCK_BEFORE = """			/* update the clk tree topology */
			__clk_set_parent_before(orphan, parent);
			__clk_set_parent_after(orphan, parent, NULL);
"""
CLOCK_AFTER = """			/* No hardware mux change: an idle orphan only needs a parent
			 * link. CLK_OPS_PARENT_ENABLE must not start an unconfigured
			 * provider (e.g. DSI PLL) just to resolve that link.
			 */
			if (orphan->prepare_count) {
				__clk_set_parent_before(orphan, parent);
				__clk_set_parent_after(orphan, parent, NULL);
			} else {
				unsigned long flags = clk_enable_lock();

				clk_reparent(orphan, parent);
				clk_enable_unlock(flags);
			}
"""


def transform_clock(original):
    if hashlib.sha256(original).hexdigest() != CLOCK_SHA256:
        raise ValueError("CCF source differs from the pinned Linux baseline")
    text = original.decode("utf-8")
    if text.count(CLOCK_BEFORE) != 1:
        raise ValueError("unexpected CCF orphan patch anchors")
    return text.replace(CLOCK_BEFORE, CLOCK_AFTER).encode("utf-8")


def stage_clock(source, native=True):
    path = source / CLOCK_PATH
    if any(p.is_symlink() for p in (path, path.parent, path.parent.parent)):
        raise ValueError("symlink CCF target")
    current = path.read_bytes()
    original = current.replace(CLOCK_AFTER.encode(), CLOCK_BEFORE.encode())
    patched = transform_clock(original)
    if current not in (original, patched):
        raise ValueError("unexpected CCF local modifications")
    path.write_bytes(patched if native else original)

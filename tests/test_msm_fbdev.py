"""Exact, reversible staging of the upstream MSM fbdev system-memory flag."""
# SPDX-License-Identifier: AGPL-3.0-only
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools import apply_msm_fbdev as port


class MsmFbdevTests(unittest.TestCase):
    def test_native_default_restage_and_unknown_edits(self):
        original = ("/* fixture */\n" + port.FBDEV_BEFORE + "return 0;\n").encode()
        with tempfile.TemporaryDirectory() as tmp, patch.object(
                port, "FBDEV_SHA256", hashlib.sha256(original).hexdigest()):
            root = Path(tmp)
            dest = root / port.FBDEV_PATH
            dest.parent.mkdir(parents=True)
            dest.write_bytes(original)
            port.stage_fbdev(root, False)
            self.assertEqual(dest.read_bytes(), original)
            port.stage_fbdev(root, True)
            expected = port.transform_fbdev(original)
            self.assertEqual(dest.read_bytes(), expected)
            port.stage_fbdev(root, True)
            self.assertEqual(dest.read_bytes(), expected)
            port.stage_fbdev(root, False)
            self.assertEqual(dest.read_bytes(), original)
            unknown = original + b"unknown edit\n"
            dest.write_bytes(unknown)
            with self.assertRaisesRegex(ValueError, "baseline"):
                port.stage_fbdev(root)
            self.assertEqual(dest.read_bytes(), unknown)

    def test_missing_duplicate_anchor_or_unknown_source_is_rejected(self):
        for data in (b"", (port.FBDEV_BEFORE * 2).encode()):
            with patch.object(port, "FBDEV_SHA256", hashlib.sha256(data).hexdigest()):
                with self.assertRaisesRegex(ValueError, "anchors"):
                    port.transform_fbdev(data)
        with self.assertRaisesRegex(ValueError, "baseline"):
            port.transform_fbdev(b"unknown source")

    def test_symlink_target_and_parent_are_rejected_without_writes(self):
        original = port.FBDEV_BEFORE.encode()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            victim = root / "victim"
            victim.write_bytes(original)
            target = root / "source" / port.FBDEV_PATH
            target.parent.mkdir(parents=True)
            try:
                target.symlink_to(victim)
            except OSError as error:
                if getattr(error, "winerror", None) == 1314:
                    self.skipTest("Windows symlink privilege required; covered on Linux")
                raise
            with self.assertRaisesRegex(ValueError, "symlink"):
                port.stage_fbdev(root / "source")
            target.unlink()
            real = root / "real"
            real.mkdir()
            target.parent.rmdir()
            target.parent.symlink_to(real, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "symlink"):
                port.stage_fbdev(root / "source")
            self.assertEqual(victim.read_bytes(), original)

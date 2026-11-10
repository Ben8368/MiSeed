"""Failure-oriented checks for the reviewed build record and preflight."""
# SPDX-License-Identifier: AGPL-3.0-only

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools import check_build_lock as lock


ROOT = Path(__file__).resolve().parents[1]


class BuildLockTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = json.loads((ROOT / lock.MANIFEST).read_text(encoding="utf-8"))
        profile = self.data["build_profiles"][0]
        for name in set(profile["input_sha256"]) | set(profile["host_tools"]):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, path)
        (self.root / "manifests").mkdir()
        self.save()

    def save(self):
        (self.root / lock.MANIFEST).write_text(json.dumps(self.data), encoding="utf-8")

    def errors(self):
        self.save()
        return lock.check_build_lock(self.root)

    def test_reviewed_record_and_hashes_match(self):
        self.assertEqual(self.errors(), [])

    def test_nfc_cannot_restore_an_android_driver_reference(self):
        self.data["build_profiles"][0]["nfc_driver_reference"] = (
            "lineage-umi-nfc-driver@6fcac56423a078c7bd180e8d2c626c7d66bf1677")
        self.assertTrue(any("pinned Linux NCI" in error for error in self.errors()))

    def test_changed_source_and_host_tool_fail(self):
        for name in ("kernel/power/umi_pps.c", "tools/collect_charge_log.py"):
            (self.root / name).write_bytes(b"changed\n")
        self.assertEqual(sum("SHA-256 mismatch" in e for e in self.errors()), 2)

    def test_removing_both_inventory_and_digest_cannot_hide_pps_input(self):
        profile = self.data["build_profiles"][0]
        path = "kernel/power/umi_pps.c"
        profile["input_sha256"].pop(path)
        profile["project_inputs"] = {k: v for k, v in profile["project_inputs"].items() if v != path}
        self.assertTrue(any("coverage" in e for e in self.errors()))

    def test_script_identity_drift_is_rejected_even_with_updated_hash(self):
        path = self.root / lock.SCRIPT
        text = path.read_text(encoding="utf-8").replace(
            'readonly kernel_revision="f45ed76d6791cf72ff1265ee93511175bbbc2f18"',
            'readonly kernel_revision="' + "a" * 40 + '"')
        path.write_text(text, encoding="utf-8", newline="\n")
        self.data["build_profiles"][0]["input_sha256"][lock.SCRIPT] = hashlib.sha256(path.read_bytes()).hexdigest()
        self.assertTrue(any("script/lock mismatch" in e for e in self.errors()))

    def test_executing_identity_is_checked_separately(self):
        self.assertTrue(any("executing build/lock mismatch" in e
                            for e in lock.check_build_lock(self.root, {"kernel_revision": "a" * 40})))

    def test_invalid_schema_duplicate_ids_and_missing_sources(self):
        original = json.dumps(self.data)
        for case in ("schema", "duplicate", "missing", "null-profile", "null-hashes"):
            with self.subTest(case=case):
                self.data = json.loads(original)
                if case == "schema":
                    self.data["schema_version"] = True
                elif case == "duplicate":
                    self.data["sources"].append(self.data["sources"][0])
                elif case == "missing":
                    self.data["sources"].pop(0)
                elif case == "null-profile":
                    self.data["build_profiles"] = [None]
                else:
                    self.data["build_profiles"][0]["input_sha256"] = None
                self.assertTrue(self.errors())

    def test_missing_file_and_invalid_json_fail_closed(self):
        (self.root / "kernel/power/umi_pps.c").unlink()
        self.assertTrue(self.errors())
        (self.root / lock.MANIFEST).write_text("{", encoding="utf-8")
        self.assertTrue(lock.check_build_lock(self.root))

    def test_escaping_paths_and_nonregular_files_are_rejected(self):
        for name in ("../outside", "/outside", "C:/outside", "kernel/../outside", "a\\b"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                lock.read_input(self.root, name)
        with self.assertRaises(ValueError):
            lock.read_input(self.root, "kernel")

    @unittest.skipIf(os.name == "nt", "POSIX symlink/FIFO fixtures")
    def test_symlink_and_fifo_are_rejected_before_reading(self):
        target = self.root / "kernel/power/umi_pps.c"
        target.unlink()
        target.symlink_to(ROOT / "kernel/power/umi_pps.c")
        self.assertTrue(any("symlink" in e for e in self.errors()))
        target.unlink()
        os.mkfifo(target)
        self.assertTrue(any("regular" in e for e in self.errors()))

    @unittest.skipUnless(os.name == "posix" and shutil.which("bash"), "POSIX Bash required")
    def test_build_rejects_drift_before_download_or_output_creation(self):
        path = self.root / "kernel/power/umi_pps.c"
        path.write_bytes(b"changed\n")
        work, output = self.root / "work", self.root / "output"
        env = dict(os.environ, MISEED_WORK_ROOT=str(work), MISEED_OUTPUT_ROOT=str(output))
        result = subprocess.run(["bash", str(self.root / lock.SCRIPT)], env=env,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("SHA-256 mismatch", result.stdout)
        self.assertFalse(work.exists())
        self.assertFalse(output.exists())

    def test_cli_returns_nonzero_for_invalid_lock(self):
        (self.root / lock.MANIFEST).write_text("{}", encoding="utf-8")
        result = subprocess.run([sys.executable, str(ROOT / "tools/check_build_lock.py"),
                                 "--root", str(self.root)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("build lock invalid", result.stdout)

"""DTBO table bounds, metadata attribution and read-only ADB acquisition."""
# SPDX-License-Identifier: AGPL-3.0-only
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from tools import collect_dtbo as collector
from tools import dtbo_table as dtbo


def fdt(properties):
    strings, tree = bytearray(), bytearray(struct.pack(">I", 1) + b"\0" * 4)
    for name, value in properties.items():
        index = len(strings)
        strings += name.encode() + b"\0"
        tree += struct.pack(">3I", 3, len(value), index) + value
        tree += b"\0" * (-len(tree) % 4)
    tree += struct.pack(">2I", 2, 9)
    header = struct.pack(">10I", 0xd00dfeed, 56 + len(tree) + len(strings), 56,
                         56 + len(tree), 40, 17, 16, 0, len(strings), len(tree))
    return header + b"\0" * 16 + tree + strings


ROOT_PROPERTIES = {"model": b"Umi test board\0", "compatible": b"xiaomi,umi\0qcom,sm8250\0",
                   "qcom,board-id": struct.pack(">2I", 0x2b, 0)}


def table(properties=None):
    payload = fdt(ROOT_PROPERTIES if properties is None else properties)
    header = struct.pack(">8I", 0xd7b7ab1e, 64 + len(payload), 32, 32, 1, 32, 4096, 0)
    entry = struct.pack(">8I", len(payload), 64, 43, 0, 0, 0, 0, 0)
    return header + entry + payload


class DtboTableTests(unittest.TestCase):
    def test_partition_padding_and_exact_metadata_comparison(self):
        blob = table() + b"\0" * 4096
        report = dtbo.inspect(blob, [0], ROOT_PROPERTIES)
        self.assertTrue(report["metadata_matches_runtime"])
        self.assertEqual(report["selected"][0]["id"], 43)
        changed = ROOT_PROPERTIES | {"model": b"different board\0"}
        self.assertFalse(dtbo.inspect(blob, [0], changed)["metadata_matches_runtime"])
        self.assertFalse(dtbo.inspect(table({}), [0], ROOT_PROPERTIES)["metadata_matches_runtime"])

    def test_table_bounds_version_and_overlap_are_rejected(self):
        for offset, value in [(0, 0), (4, 1), (8, 16), (12, 16), (16, 4097),
                              (20, 0), (24, 3), (28, 1), (32, 0), (36, 0)]:
            blob = bytearray(table())
            struct.pack_into(">I", blob, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                dtbo.table_entries(bytes(blob))
        blob = bytearray(table())
        payload = blob[64:]
        blob = blob[:64] + blob[32:64] + payload
        struct.pack_into(">I", blob, 4, len(blob))
        struct.pack_into(">I", blob, 16, 2)
        struct.pack_into(">I", blob, 36, 96)
        struct.pack_into(">I", blob, 68, 96)
        with self.assertRaisesRegex(ValueError, "overlapping"):
            dtbo.table_entries(bytes(blob))

    def test_invalid_and_out_of_range_selection_is_rejected(self):
        for text in ("", "-1", "1 2", "0,0", "01", "0;reboot", "1\n2"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                dtbo.selected_indices(text)
        for indices in ([1], [-1], [], [0, 0]):
            with self.subTest(indices=indices), self.assertRaises(ValueError):
                dtbo.inspect(table(), indices)


class ReadOnlyAcquisitionTests(unittest.TestCase):
    def simulate(self, root, product=b"umi", selection_after=None,
                 initial_uid=b"2000", root_mode="works", su_available=True,
                 read_failure=False):
        calls, selection_reads = [], 0
        state = {"uid": initial_uid}
        def adb_read(adb, serial, *command, **kwargs):
            nonlocal selection_reads
            calls.append(command)
            if command == ("exec-out", "id", "-u"):
                return state["uid"]
            if command == ("root",):
                if root_mode != "denied":
                    state["uid"] = b"0"
                if root_mode == "timeout":
                    raise subprocess.TimeoutExpired("adb", 1)
                return b""
            if command == ("unroot",):
                if root_mode != "restore-fails":
                    state["uid"] = b"2000"
                return b""
            if command == ("shell", "getprop", "ro.product.device"):
                return product
            if command == ("shell", "getprop", "ro.boot.dtbo_idx"):
                selection_reads += 1
                return b"0" if selection_reads == 1 or selection_after is None else selection_after
            if command == ("exec-out", "su", "-c", "id -u"):
                return b"0" if su_available else b"su: inaccessible or not found\n"
            if command[-1].endswith("/proc/sys/kernel/random/boot_id"):
                return b"12345678-1234-1234-1234-123456789abc"
            for prop in ("model", "compatible", "qcom,board-id", "qcom,msm-id"):
                if command[-1].endswith("/" + prop):
                    return ROOT_PROPERTIES.get(prop, struct.pack(">2I", 356, 0))
            raise AssertionError(command)
        def run(command, **kwargs):
            calls.append(tuple(command[1:]))
            if command[-1] == "devices":
                return SimpleNamespace(stdout="List of devices attached\nprivate-device-id\tdevice\n")
            if command[-1] != "head -c 67108865 /dev/block/by-name/dtbo":
                self.assertEqual(command[-4:], ["head", "-c", "67108865", "/dev/block/by-name/dtbo"])
            if read_failure:
                raise subprocess.TimeoutExpired("adb", 1)
            kwargs["stdout"].write(table())
            return SimpleNamespace(returncode=0)
        def wait_uid(adb, serial, expected):
            if state["uid"] != expected:
                raise ValueError("expected privilege identity unavailable")
        return (calls, patch.object(collector, "ROOT", root),
                patch.object(collector, "adb_read", adb_read),
                patch.object(collector.subprocess, "run", run),
                patch.object(collector, "wait_uid", wait_uid))

    def test_successful_capture_is_read_only_and_does_not_store_serial(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            calls, p1, p2, p3, p4 = self.simulate(root)
            with p1, p2, p3, p4:
                report = collector.collect(Path("adb"), root / ".work/capture")
            self.assertTrue(report["result"].startswith("PASS"))
            self.assertNotIn("private-device-id", json.dumps(report))
            self.assertFalse((root / ".work/capture/dtbo.img.partial").exists())
            self.assertTrue((root / ".work/capture/dtbo.img").exists())
            for command in calls:
                self.assertFalse(any(word in command for word in ("reboot", "root", "remount", "push", "flash")))

    def test_wrong_device_and_changed_boot_selection_never_publish_success(self):
        for product, after in ((b"cmi", None), (b"umi", b"1")):
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                calls, p1, p2, p3, p4 = self.simulate(root, product, after)
                with p1, p2, p3, p4, self.assertRaises(ValueError):
                    collector.collect(Path("adb"), root / ".work/capture")
                self.assertFalse((root / ".work/capture/evidence.json").exists())
                self.assertFalse((root / ".work/capture/dtbo.img").exists())

    def test_existing_root_and_explicit_temporary_root_preserve_initial_identity(self):
        for uid, temporary in ((b"0", False), (b"2000", True)):
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                calls, p1, p2, p3, p4 = self.simulate(root, initial_uid=uid)
                with p1, p2, p3, p4:
                    report = collector.collect(Path("adb"), root / ".work/capture", temporary)
                self.assertEqual(report["adb_uid_before"], uid.decode())
                self.assertEqual(report["adb_uid_after"], uid.decode())
                self.assertEqual(("root",) in calls, temporary)
                self.assertEqual(("unroot",) in calls, temporary)

    def test_without_explicit_flag_missing_su_never_elevates(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            calls, p1, p2, p3, p4 = self.simulate(root, su_available=False)
            with p1, p2, p3, p4, self.assertRaisesRegex(ValueError, "no existing root"):
                collector.collect(Path("adb"), root / ".work/capture")
            self.assertNotIn(("root",), calls)
            self.assertNotIn(("unroot",), calls)
            self.assertFalse((root / ".work/capture").exists())

    def test_elevation_denial_read_timeout_and_restore_failure_never_publish_success(self):
        for mode, failed in (("denied", False), ("timeout", False),
                             ("works", True), ("restore-fails", False)):
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                calls, p1, p2, p3, p4 = self.simulate(root, root_mode=mode, read_failure=failed)
                with p1, p2, p3, p4, self.assertRaises((ValueError, subprocess.SubprocessError)):
                    collector.collect(Path("adb"), root / ".work/capture", True)
                self.assertIn(("unroot",), calls)
                self.assertFalse((root / ".work/capture/evidence.json").exists())
                self.assertFalse((root / ".work/capture/dtbo.img").exists())

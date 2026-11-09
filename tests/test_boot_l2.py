"""Offline regressions for pinned Linux inputs and the macOS build wrapper."""

# SPDX-License-Identifier: AGPL-3.0-only

import hashlib
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools import verify_kernel_source as verifier


ROOT = Path(__file__).resolve().parents[1]


class SourceCacheTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / "repo"
        self.repo.mkdir()
        self.git("init", "-q")
        for name, text in {verifier.MAKEFILE: "# upstream rules\n",
                           "drivers/mfd/Makefile": "# MFD upstream rules\n",
                           "drivers/power/supply/Makefile": "# upstream power rules\n",
                           "drivers/power/supply/Kconfig": "# upstream power config\n",
                           "include/board.dtsi": "original\n",
                           ".gitignore": "*.o\n"}.items():
            path = self.repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8", newline="\n")
        self.git("-c", "core.autocrlf=false", "add", ".")
        self.git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "commit", "-qm", "fixture")
        self.revision = self.git("rev-parse", "HEAD").strip()
        self.source = self.root / "source"
        shutil.copytree(self.repo, self.source, ignore=shutil.ignore_patterns(".git"))
        (self.source / ".miseed-source-revision").write_text(self.revision)

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.repo), *args],
                                       encoding="utf-8")

    def verify(self):
        verifier.verify_source(self.repo, self.revision, self.source)

    def test_pristine_and_known_project_overlays(self):
        self.verify()
        for name in verifier.GENERATED - {".miseed-source-revision"}:
            (self.source / name).parent.mkdir(parents=True, exist_ok=True)
            (self.source / name).write_text("generated\n")
        with (self.source / verifier.MAKEFILE).open("a", newline="\n") as stream:
            stream.write("\ndtb-y += umi-csot.dtb\nDTC_FLAGS_umi-csot += -@\n")
        self.verify()

    def test_mfd_observer_rule_is_allowed_but_other_edits_are_rejected(self):
        path = self.source / "drivers/mfd/Makefile"
        with path.open("a", newline="\n") as stream:
            stream.write("\nobj-$(CONFIG_MFD_SPMI_PMIC) += miseed_power_observer.o\n")
        self.verify()
        with path.open("a", newline="\n") as stream:
            stream.write("obj-y += unrelated.o\n")
        with self.assertRaisesRegex(ValueError, "differs from pinned commit"):
            self.verify()

    def test_modified_included_dtsi_with_matching_revision_is_rejected(self):
        (self.source / "include/board.dtsi").write_text("tampered\n")
        with self.assertRaisesRegex(ValueError, "differs from pinned commit"):
            self.verify()

    def test_missing_tracked_input_is_rejected(self):
        (self.source / "include/board.dtsi").unlink()
        with self.assertRaises(OSError):
            self.verify()

    def test_ignored_extra_build_input_is_rejected(self):
        (self.source / "untracked.o").write_bytes(b"stale object")
        with self.assertRaisesRegex(ValueError, "unexpected cached source input"):
            self.verify()

    def test_unrelated_makefile_change_is_rejected(self):
        with (self.source / verifier.MAKEFILE).open("a", newline="\n") as stream:
            stream.write("obj-y += injected.o\n")
        with self.assertRaisesRegex(ValueError, "differs from pinned commit"):
            self.verify()

    def test_wrong_marker_is_rejected(self):
        (self.source / ".miseed-source-revision").write_text("0" * 40)
        with self.assertRaisesRegex(ValueError, "marker mismatch"):
            self.verify()

    @unittest.skipIf(os.name == "nt", "POSIX FIFO fixture")
    def test_revision_marker_fifo_is_rejected_without_reading(self):
        marker = self.source / ".miseed-source-revision"
        marker.unlink()
        os.mkfifo(marker)
        with self.assertRaisesRegex(ValueError, "not a regular file"):
            self.verify()

    def test_cli_fails_closed_for_dirty_cache(self):
        (self.source / "include/board.dtsi").write_bytes(b"changed")
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools/verify_kernel_source.py"),
             "--repository", str(self.repo), "--revision", self.revision,
             "--source", str(self.source)], capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("source cache verification failed", result.stderr)

    @unittest.skipIf(os.name == "nt", "POSIX executable modes")
    def test_changed_executable_mode_is_rejected(self):
        (self.source / "include/board.dtsi").chmod(0o755)
        with self.assertRaisesRegex(ValueError, "changed executable mode"):
            self.verify()

    @unittest.skipIf(os.name == "nt", "POSIX symlink fixture")
    def test_overlay_symlink_is_rejected(self):
        (self.source / "arch/arm64/boot/dts/qcom/umi-csot.dts").symlink_to(
            self.source / "include/board.dtsi")
        with self.assertRaisesRegex(ValueError, "not a regular file"):
            self.verify()

    def test_generated_file_path_directory_is_rejected(self):
        path = self.source / "arch/arm64/boot/dts/qcom/umi-csot.dts"
        path.mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, "not a regular file"):
            self.verify()


    def run_cache_preparation(self, dirty, fail_after_selection=False):
        work = self.root / "work"
        work.mkdir()
        cached_source = work / ("linux-source-" + self.revision)
        self.source.rename(cached_source)
        self.source = cached_source
        build = work / ("build-" + self.revision)
        build.mkdir()
        (build / "stale-object.o").write_bytes(b"old build")
        if dirty:
            (cached_source / "include/board.dtsi").write_bytes(b"edited")
        template = b"fixture header"
        (work / "reference-boot-v2.img").write_bytes(template)
        tools = self.root / "bin"
        tools.mkdir()
        for name, output in [("aarch64-linux-gnu-gcc", "13.3.0"), ("aarch64-linux-gnu-nm", ""),
                             ("make", "GNU Make 4.3"),
                             ("dtc", ""), ("fdtget", ""), ("fdtput", ""), ("fdtoverlay", "")]:
            stub = tools / name
            stub.write_text("#!/bin/sh\nprintf '%s\\n' " + shlex.quote(output) + "\n")
            stub.chmod(0o755)
        script = (ROOT / "tools/build_kernel_l2.sh").read_text()
        inputs = self.root / "inputs"
        record = json.loads((ROOT / "manifests/sources.lock.json").read_text())
        profile = record["build_profiles"][0]
        for name in set(profile["input_sha256"]) | set(profile["host_tools"]):
            destination = inputs / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, destination)
        script = script.replace('readonly kernel_revision="' +
                                record["sources"][0]["revision"] + '"',
                                'readonly kernel_revision="' + self.revision + '"')
        script = script.replace('readonly boot_sha256="' + profile["boot_header_template"]["sha256"] + '"',
                                'readonly boot_sha256="' + hashlib.sha256(template).hexdigest() + '"')
        (inputs / "tools/build_kernel_l2.sh").write_text(script, encoding="utf-8", newline="\n")
        record["sources"][0]["revision"] = self.revision
        profile["kernel_source"] = "linux-stable@" + self.revision
        profile["boot_header_template"]["sha256"] = hashlib.sha256(template).hexdigest()
        profile["input_sha256"]["tools/build_kernel_l2.sh"] = hashlib.sha256(
            (inputs / "tools/build_kernel_l2.sh").read_bytes()).hexdigest()
        (inputs / "manifests").mkdir()
        (inputs / "manifests/sources.lock.json").write_text(json.dumps(record), encoding="utf-8")
        script = script[:script.index("readonly qcom_dts_dir=")]
        lines = script.splitlines()
        for index, line in enumerate(lines):
            if line.startswith("readonly repo_root="):
                lines[index] = "readonly repo_root=" + shlex.quote(str(inputs))
            elif line.startswith("readonly kernel_revision="):
                lines[index] = "readonly kernel_revision=" + self.revision
            elif line.startswith("readonly boot_sha256="):
                lines[index] = "readonly boot_sha256=" + hashlib.sha256(template).hexdigest()
        lines.append('python3 "$repo_root/tools/verify_kernel_source.py" --repository "$source_repository" --revision "$kernel_revision" --source "$source_tree"')
        lines.append('printf "SELECTED:%s\\nSELECTED:%s\\n" "$source_tree" "$build_tree"')
        if fail_after_selection:
            lines.append("exit 17")
        entry = self.root / "prepare.sh"
        entry.write_text("\n".join(lines) + "\n")
        env = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ["PATH"],
                   MISEED_KERNEL_SOURCE=str(self.repo), MISEED_WORK_ROOT=str(work),
                   MISEED_OUTPUT_ROOT=str(self.root / "output"), MISEED_JOBS="1")
        env.pop("MISEED_BUILD_ROOT", None)
        env.pop("MISEED_EXTRACTED_SOURCE", None)
        result = subprocess.run(["bash", entry], env=env, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 17 if fail_after_selection else 0, result.stderr)
        selected = [Path(line.removeprefix("SELECTED:")) for line in result.stdout.splitlines()
                    if line.startswith("SELECTED:")]
        self.assertEqual(len(selected), 2, result.stdout)
        return cached_source, build, selected

    @unittest.skipUnless(sys.platform == "linux" and shutil.which("bash"), "Linux Bash required")
    def test_build_script_reuses_verified_source_and_build_cache(self):
        source, build, selected = self.run_cache_preparation(dirty=False)
        self.assertEqual(selected, [source, build])

    @unittest.skipUnless(sys.platform == "linux" and shutil.which("bash"), "Linux Bash required")
    def test_build_script_falls_back_without_reusing_or_deleting_dirty_cache(self):
        source, build, selected = self.run_cache_preparation(dirty=True)
        self.assertNotEqual(selected[0], source)
        self.assertNotEqual(selected[1], build)
        self.assertEqual((source / "include/board.dtsi").read_bytes(), b"edited")
        self.assertEqual((build / "stale-object.o").read_bytes(), b"old build")
        self.assertFalse(selected[0].exists())
        self.assertFalse(selected[1].exists())

    @unittest.skipUnless(sys.platform == "linux" and shutil.which("bash"), "Linux Bash required")
    def test_temporary_fallback_trees_are_removed_after_failure(self):
        source, build, selected = self.run_cache_preparation(dirty=True, fail_after_selection=True)
        self.assertNotEqual(selected[0], source)
        self.assertNotEqual(selected[1], build)
        self.assertFalse(selected[0].exists())
        self.assertFalse(selected[1].exists())
        self.assertEqual((source / "include/board.dtsi").read_bytes(), b"edited")
        self.assertEqual((build / "stale-object.o").read_bytes(), b"old build")


@unittest.skipUnless(os.name == "posix" and shutil.which("bash"), "POSIX Bash required")
class MacOSWrapperTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.log = self.root / "docker.jsonl"
        self.env = dict(os.environ, PATH=str(self.root) + os.pathsep + os.environ["PATH"],
                        TEST_DOCKER_LOG=str(self.log), TEST_ARCH="arm64", MISEED_JOBS="2",
                        MISEED_COLIMA_PROFILE="fixture")
        scripts = {
            "uname": 'import os,sys; print("Darwin" if sys.argv[1] == "-s" else os.environ["TEST_ARCH"])',
            "colima": 'print("running")',
            "docker": (
                'import json,os,sys\n'
                'with open(os.environ["TEST_DOCKER_LOG"], "a") as stream:\n'
                '    stream.write(json.dumps(sys.argv[1:]) + "\\n")\n'
                'sys.exit(7 if os.environ.get("TEST_BUILD_FAIL") and "build" in sys.argv else 0)'
            ),
        }
        for name, script in scripts.items():
            path = self.root / name
            path.write_text("#!" + sys.executable + "\n" + script + "\n")
            path.chmod(0o755)

    def run_wrapper(self):
        return subprocess.run(["bash", str(ROOT / "tools/build_kernel_l2_macos.sh")],
                              cwd=self.root, env=self.env, text=True, capture_output=True)

    def test_outside_repository_and_architecture_isolated_cache(self):
        volumes = []
        for machine, arch in [("arm64", "arm64"), ("x86_64", "amd64")]:
            with self.subTest(machine=machine):
                self.env["TEST_ARCH"] = machine
                self.log.unlink(missing_ok=True)
                result = self.run_wrapper()
                self.assertEqual(result.returncode, 0, result.stderr)
                calls = [json.loads(line) for line in self.log.read_text().splitlines()]
                build = next(call for call in calls if "build" in call)
                dockerfile = Path(build[build.index("--file") + 1])
                self.assertEqual(dockerfile, ROOT / "tools/macos-l2.Dockerfile")
                self.assertTrue(dockerfile.is_file())
                volume = next(call[-1] for call in calls if "volume" in call)
                volumes.append(volume)
                self.assertTrue(volume.endswith("-" + arch))
                run = next(call for call in calls if "run" in call)
                self.assertIn(f"type=volume,source={volume},target=/build", run)
                self.assertIn(f"MISEED_OUTPUT_ROOT=/workspace/.work/boot-l2-{arch}", run)
        self.assertNotEqual(*volumes)

    def test_failed_image_build_does_not_run_container(self):
        self.env["TEST_BUILD_FAIL"] = "1"
        self.assertEqual(self.run_wrapper().returncode, 7)
        calls = [json.loads(line) for line in self.log.read_text().splitlines()]
        self.assertFalse(any("run" in call or "volume" in call for call in calls))

    def test_invalid_jobs_fails_before_docker(self):
        self.env["MISEED_JOBS"] = "0"
        self.assertNotEqual(self.run_wrapper().returncode, 0)
        self.assertFalse(self.log.exists())


if __name__ == "__main__":
    unittest.main()

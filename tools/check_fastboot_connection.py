"""One-shot, bounded Fastboot/USB diagnosis; never boot, reset or write a device."""
# SPDX-License-Identifier: AGPL-3.0-only

import argparse
import json
from pathlib import Path
import plistlib
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
FASTBOOT = ROOT / ".work/host-tools/platform-tools/fastboot"
EXIT_CODES = {
    "ready_for_identity": 0, "multiple_devices": 2,
    "invalid_enumeration": 3, "usb_interfaces_unavailable": 4,
    "fastboot_access_failed": 5, "not_detected": 6,
    "usb_visibility_unverified": 7, "fastboot_tool_failed": 8,
}
MESSAGES = {
    "ready_for_identity": "唯一 Fastboot 设备可访问；下一步核对产品、解锁状态和模式。",
    "multiple_devices": "发现多台设备；停止，不猜目标。",
    "invalid_enumeration": "Fastboot 返回格式异常；停止检查工具输出，设备标识不公开。",
    "usb_interfaces_unavailable": "USB 有目标设备，但未发布 Fastboot 接口；停止空扫，检查 USB 配置/连接状态。",
    "fastboot_access_failed": "USB 已发布 Fastboot 接口，但工具未列出；检查接口访问、占用及工具。",
    "not_detected": "主机 USB 未发现预期目标；确认模式和连接，一次只改变一个因素。",
    "usb_visibility_unverified": "USB 可见性未核实；需在具有主机 USB 访问权限的环境对照。",
    "fastboot_tool_failed": "Fastboot 命令失败或超时；停止，不循环等待设备。",
}


def run_readonly(command, timeout, runner):
    try:
        result = runner(command, capture_output=True, timeout=timeout)
        return result.returncode, result.stdout, ""
    except subprocess.TimeoutExpired:
        return None, b"", "timeout"
    except OSError:
        return None, b"", "unavailable"


def usb_summary(tree):
    """Only summarize the expected Umi Fastboot VID/PID; discard all identifiers."""
    summary = {"target_count": 0, "published_interfaces": 0, "fastboot_interfaces": 0}

    def interfaces(node):
        if not isinstance(node, dict):
            return
        if "bInterfaceClass" in node:
            summary["published_interfaces"] += 1
            if tuple(node.get(k) for k in (
                "bInterfaceClass", "bInterfaceSubClass", "bInterfaceProtocol"
            )) == (0xff, 0x42, 0x03):
                summary["fastboot_interfaces"] += 1
        for child in node.get("IORegistryEntryChildren", []):
            interfaces(child)

    def devices(node):
        if isinstance(node, list):
            for child in node:
                devices(child)
        elif isinstance(node, dict):
            if node.get("idVendor") == 0x18d1 and node.get("idProduct") == 0xd00d:
                summary["target_count"] += 1
                interfaces(node)
            else:
                for child in node.get("IORegistryEntryChildren", []):
                    devices(child)

    if not isinstance(tree, (list, dict)):
        raise ValueError("invalid USB tree")
    devices(tree)
    return summary


def inspect_connection(fastboot=FASTBOOT, timeout=2, runner=subprocess.run, platform=None):
    """Return sanitized report and a separate ephemeral selector, never serialized."""
    if not 0 < timeout <= 5:
        raise ValueError("timeout must be greater than zero and at most five seconds")
    started = time.monotonic()
    code, output, error = run_readonly([str(fastboot), "devices"], timeout, runner)
    if isinstance(output, bytes):
        output = output.decode("utf-8", errors="replace")
    rows = [line.split() for line in output.splitlines() if line.strip()]
    report = {"fastboot_exit": code, "fastboot_error": error,
              "fastboot_count": len(rows), "usb": {"access": "not_checked"}}
    selector = None
    if code != 0:
        state = "fastboot_tool_failed"
    elif any(len(row) != 2 or row[1] != "fastboot" for row in rows):
        state = "invalid_enumeration"
    elif len(rows) > 1:
        state = "multiple_devices"
    elif rows:
        state = "ready_for_identity"
        selector = rows[0][0]
    else:
        state = "usb_visibility_unverified"
        if (platform or sys.platform) == "darwin":
            usb_code, raw, usb_error = run_readonly([
                "/usr/sbin/ioreg", "-a", "-l", "-r", "-c", "IOUSBHostDevice"
            ], timeout, runner)
            report["usb"] = {"access": "unverified", "exit": usb_code, "error": usb_error}
            if usb_code == 0:
                try:
                    usb = usb_summary(plistlib.loads(raw) if raw.strip() else [])
                    report["usb"].update(usb, access="ok")
                    state = ("not_detected" if not usb["target_count"] else
                             "fastboot_access_failed" if usb["fastboot_interfaces"] else
                             "usb_interfaces_unavailable")
                except (ValueError, TypeError, plistlib.InvalidFileException):
                    report["usb"]["error"] = "invalid_usb_report"
    report.update(state=state, message=MESSAGES[state],
                  elapsed_seconds=round(time.monotonic() - started, 3))
    return report, selector


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fastboot", type=Path, default=FASTBOOT)
    parser.add_argument("--timeout", type=float, default=2,
                        help="per-command deadline in seconds (0 < timeout <= 5)")
    args = parser.parse_args(argv)
    if not 0 < args.timeout <= 5:
        parser.error("timeout must be greater than zero and at most five seconds")
    report, _ = inspect_connection(args.fastboot, args.timeout)
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return EXIT_CODES[report["state"]]


if __name__ == "__main__":
    raise SystemExit(main())

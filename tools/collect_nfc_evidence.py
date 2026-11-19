"""One-shot Windows capture of standard NFC/NCI L3; no retries or VEN reset."""
# SPDX-License-Identifier: AGPL-3.0-only
import argparse
from pathlib import Path
import re

if __package__:
    from .collect_display_evidence import (ExportError, capture, nfc_l3_result,
                                          open_windows_port, prepare_output, validate_frame, write_report)
else:
    from collect_display_evidence import (ExportError, capture, nfc_l3_result,
                                         open_windows_port, prepare_output, validate_frame, write_report)


def check_identity(frame):
    lines = frame.splitlines()

    def only(prefix, pattern):
        records = [line for line in lines if line.startswith(prefix)]
        match = re.fullmatch(pattern, records[0]) if len(records) == 1 else None
        if not match:
            raise ExportError(f"missing, ambiguous or invalid {prefix.decode()} record; probe stopped")
        return match

    if b'truncated' in frame:
        raise ExportError("truncated NFC inventory; probe stopped")
    device = only(b'NFC I2C device', rb'NFC I2C device=(\d+-0028) driver=sn100x')[1]
    only(b'NFC ' + device + b' reg', rb'NFC ' + re.escape(device) + rb' reg=0x28')
    only(b'NFC DT matches', rb'NFC DT matches=1 scan=finished')
    only(b'NFC class device=', rb'NFC class device=nfc\d+ \(registration metadata only\)')
    only(b'NFC class devices', rb'NFC class devices=1 \(all controllers, not a transport test\)')


def probe_steps(frame, read_fixture):
    """Classify only milestones observed inside one validated, complete probe frame."""
    stages = [
        ('reset_init', rb'NFC DEV_UP/reset-init passed\.', rb'NFC DEV_UP/reset-init failed: .*'),
        ('empty_protocol', rb'NFC empty-protocol request rejected: .* \(-22\)\.',
         rb'NFC (?:empty-protocol check failed: .*|unsupported-protocol negative case unexpectedly accepted\.)'),
        ('rf_discovery', rb'NFC RF discovery passed: targets=[1-8] protocol=\d+; UID omitted\.',
         rb'NFC (?:(?:START_POLL|GET_TARGET) failed: .*|RF discovery found no target; .*)'),
    ]
    if read_fixture:
        stages.append(('ndef_fixture', rb'NFC NDEF Type 4 fixture verified; no UID or payload logged\.',
                       rb'NFC read Type 4 fixture failed: .*'))
    stages.append(('close_reopen', rb'NFC close/CORE_RESET/reopen/final close passed; shared VEN was retained\.',
                   rb'NFC (?:STOP_POLL|DEV_DOWN/CORE_RESET close|clean reopen|final DEV_DOWN) failed: .*'))
    checks = {name: 'NO_EVIDENCE' for name, _, _ in stages}
    if not read_fixture:
        checks['ndef_fixture'] = 'NOT_REQUESTED'
    previous = -1
    prefix_passed = True
    lines = frame.splitlines()
    for name, passed, failed in stages:
        successes = [i for i, line in enumerate(lines) if re.fullmatch(passed, line)]
        failures = [i for i, line in enumerate(lines) if re.fullmatch(failed, line)]
        if successes:
            if failures or len(successes) != 1 or successes[0] <= previous or not prefix_passed:
                raise ExportError('inconsistent NFC milestone: ' + name)
            checks[name] = 'PASS'
            previous = successes[0]
        else:
            prefix_passed = False
            if failures:
                checks[name] = 'FAIL'
    return checks


def collect(port, output, report, *, run_rf, read_fixture=False, first=False, waveform_armed=False, init_retry=False):
    if init_retry and (first or waveform_armed or run_rf or read_fixture):
        raise ExportError("init-retry captures only one DEV_UP and kernel logs")
    if first and (run_rf or not waveform_armed) or waveform_armed and not first:
        raise ExportError("first-address probe requires an armed waveform capture and no RF probe")
    if read_fixture and not run_rf:
        raise ExportError("fixture read requires an authorized active probe")
    status = output / "nfc-status.txt"
    capture(port, status, "nfc-status", b"NFC_STATUS")
    check_identity(validate_frame(status.read_bytes(), b"NFC_STATUS"))
    if first or init_retry:
        probe = output / 'nfc-first.txt'
        capture(port, probe, 'nfc-first', b'NFC_FIRST')
        frame = validate_frame(probe.read_bytes(), b'NFC_FIRST')
        match = re.fullmatch(rb'NFC_FIRST_BEGIN\nNFC_FIRST_RESULT stage=(dev_up|setup|guard) rc=(0|-[1-9]\d*)\nNFC_FIRST_END\n', frame)
        if not match or match[2] == b'0' and match[1] != b'dev_up':
            raise ExportError('invalid first-transfer result')
        report.update(stage=match[1].decode(), probe_rc=int(match[2]), ninth_clock='NOT_MEASURED_BY_HOST')
        if report['probe_rc']:
            report['issues'].append({'section': report['stage'], 'message': 'first-transfer probe failed'})
        capture(port, output / 'dmesg.txt', 'dmesg', b'KMSG')
        return
    if not run_rf:
        capture(port, output / "dmesg.txt", "dmesg", b"KMSG")
        return
    probe = output / "nfc-l3.txt"
    capture(port, probe, "nfc-ndef" if read_fixture else "nfc-l3", b"NFC_L3", timeout=120)
    frame = validate_frame(probe.read_bytes(), b'NFC_L3')
    report["probe_rc"] = nfc_l3_result(frame)
    try:
        report['steps'] = probe_steps(frame, read_fixture)
    except ExportError as exc:
        report['issues'].append({'section': 'milestones', 'message': str(exc)})
    if report["probe_rc"]:
        report["issues"].append({"section": "nfc_l3", "message": "NCI/RF/reopen probe failed"})
    elif any(v not in ('PASS', 'NOT_REQUESTED') for v in report.get('steps', {}).values()):
        report['issues'].append({'section': 'milestones', 'message': 'missing or failed required NFC milestone'})
    # Only send another command after observing a complete probe frame.
    capture(port, output / "dmesg.txt", "dmesg", b"KMSG")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", type=Path, required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--init-retry', action='store_true', help='one DEV_UP for the bounded reset experiment')
    mode.add_argument('--first-address', action='store_true', help='one DEV_UP only; no RF or reopen')
    parser.add_argument('--waveform-armed', action='store_true', help='confirm external synchronized capture is armed')
    mode.add_argument("--run-rf", action="store_true",
                        help="explicitly run one authorized RF probe with a non-payment test tag")
    parser.add_argument("--read-fixture", action="store_true", help="with --run-rf: verify one Type 4 MiSeed test tag")
    args = parser.parse_args()
    if args.first_address != args.waveform_armed:
        parser.error('--first-address and --waveform-armed must be specified together')
    if args.read_fixture and not args.run_rf:
        parser.error("--read-fixture requires --run-rf")
    output = prepare_output(args.output, parser)
    output.mkdir(parents=True)
    report = {"mode": "init-retry" if args.init_retry else "first-address" if args.first_address else "ndef-fixture" if args.read_fixture else "active" if args.run_rf else "passive", "probe_rc": None, "issues": []}
    try:
        port = open_windows_port(args.port)
        try:
            collect(port, output, report, run_rf=args.run_rf, read_fixture=args.read_fixture,
                    first=args.first_address, waveform_armed=args.waveform_armed, init_retry=args.init_retry)
        finally:
            port.close()
    except (ExportError, OSError) as exc:
        report["issues"].append({"section": "capture", "message": str(exc)})
    report["result"] = ("FAIL" if report["issues"] else "PASS DEV_UP only" if args.init_retry else "PASS DEV_UP only; consult waveform" if args.first_address else
                        "PASS scoped Type 4 fixture/RF/reopen" if args.read_fixture else
                        "PASS scoped NCI/RF/reopen" if args.run_rf else "PASS passive registration only")
    return write_report(output, report)


if __name__ == "__main__":
    raise SystemExit(main())

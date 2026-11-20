"""Validate a retained MiSeed RX/NFC coexistence experiment."""

# SPDX-License-Identifier: AGPL-3.0-only

import re

from .collect_charge_log import BEGIN, END, ExportError


RECORD = re.compile(rb"seq=(\d+) boot_ms=(\d+) ([A-Z_]+) (.+)")
KMSG = re.compile(rb"\d+,\d+,(\d+),[^;]*;(.+)")
VALUE = re.compile(rb"(?:^|\s)([a-z_]+)=(.*?)(?=\s+[a-z_]+=|$)")
RX_PG = re.compile(rb"rx1619 \d+-0061: power-good changed, pg=(-?\d+)(?:\s|$)")
RF_IDLE = re.compile(rb"miseed-rx-nfc rf_idle previous=([a-z]+)(?:\s|$)")
TYPEC_MARK = b"miseed-rx-diag smb5"
TYPEC_HW = re.compile(rb"_off=1 cc_attached=0 vbus_present=([01]) cc_status=0x([0-9a-f]{2})\b")
PARTNER_ABSENT = b"port=port0 partner=0 rc=0"

MILESTONES = (
    b"stage=detached online=0", b"stage=seated online=1",
    b"stage=blocked start_poll=ERFKILL", b"stage=removed online=0",
    b"stage=rediscovered targets=1 mifare=1",
)


def tcpc_vbus(body):
    match = TYPEC_HW.match(body.split(TYPEC_MARK, 1)[1])
    if not match or int(match[2], 16) & 0x21 != int(match[1]) << 5:
        raise ExportError("Conflicting or unverified Type-C CC/VBUS evidence")
    return int(match[1])


def assess(raw, mode):
    if mode not in ("poll", "target"):
        raise ValueError("invalid coexistence test mode")
    lines = raw.splitlines()
    header = BEGIN.fullmatch(lines[0]) if lines else None
    tail = END.fullmatch(lines[-1]) if lines else None
    if not header or not tail:
        raise ExportError("Missing complete charge-log frame")
    first, next_seq, count = map(int, header.groups()[:3])
    if (next_seq != int(tail[1]) or next_seq - first != count or count != len(lines) - 2
            or int(header[4] or b"0") or int(header[5] or b"0")):
        raise ExportError("Charge-log bounds, loss or recorder error")

    records, kernel = [], []
    for seq, line in enumerate(lines[1:-1], first):
        match = RECORD.fullmatch(line)
        if not match or int(match[1]) != seq:
            raise ExportError("Invalid charge-log record order")
        timestamp, kind, body = int(match[2]), match[3], match[4]
        # Ring overruns have a drain time but no reliable event-time bounds.
        if kind == b"LOSS" or (kind == b"ERROR" and body.startswith(b"kernel_read_errno=")):
            raise ExportError("Kernel log loss or read error in retained frame")
        records.append((timestamp, kind, body))
        if kind == b"KERNEL":
            event = KMSG.fullmatch(body.rstrip())
            if not event:
                raise ExportError("Missing original kernel event time")
            kernel.append((int(event[1]), event[2]))

    mode = mode.encode("ascii")
    stages = [(i, body) for i, (_, kind, body) in enumerate(records) if kind == b"RX_NFC"]
    expected = (b"stage=armed mode=" + mode, *MILESTONES,
                b"stage=result mode=" + mode + b" at=rediscover rc=0 cleanup=0")
    if tuple(body for _, body in stages) != expected:
        raise ExportError("RX/NFC experiment incomplete or failed: " +
                          repr([body.decode("ascii", errors="replace") for _, body in stages]))

    times = [records[i][0] for i, _ in stages]
    if times != sorted(times):
        raise ExportError("RX/NFC stage time reversed")
    armed_ms, detached_ms, seated_ms, _, removed_ms, _, finished_ms = times
    seated, removed = stages[2][0], stages[4][0]
    cycle_us = [(us, body) for us, body in kernel if detached_ms <= us // 1000 <= finished_ms]
    cycle = [(us // 1000, body) for us, body in cycle_us]
    pg_edges = [(us, int(match[1])) for us, body in cycle_us if (match := RX_PG.match(body))]
    idle = [(us, match[1]) for us, body in cycle_us if (match := RF_IDLE.match(body))]
    # Initial unplug needs a post-arm, recent hardware observation with no VBUS.
    initial = [body for us, body in kernel if max(armed_ms, detached_ms - 2000) <= us // 1000 <= detached_ms
               and TYPEC_MARK in body]
    if not initial or tcpc_vbus(initial[-1]):
        raise ExportError("No confirmed Type-C unplug before seating")
    cc = [(us, tcpc_vbus(body)) for us, body in cycle_us if TYPEC_MARK in body]
    if not any(seated_ms <= us // 1000 <= removed_ms for us, _ in cc):
        raise ExportError("No corroborating Type-C CC/VBUS sample while seated")
    if any(us // 1000 > removed_ms and vbus for us, vbus in cc):
        raise ExportError("VBUS detected after off-pad removal confirmation")
    # A later periodic sample ensures the recorder drained past the result.
    if not any(us // 1000 > finished_ms and TYPEC_MARK in body for us, body in kernel):
        raise ExportError("Kernel drain has not reached the experiment result")
    prior = b"discovery" if mode == b"poll" else b"target"
    if (len(pg_edges) != 2 or [value for _, value in pg_edges] != [1, 0]
            or pg_edges[0][0] // 1000 > removed_ms or pg_edges[1][0] // 1000 < seated_ms):
        raise ExportError("Missing unambiguous RX power-good rise and fall")
    pg_on_us, pg_off_us = (us for us, _ in pg_edges)
    if not any(us >= pg_off_us and not vbus for us, vbus in cc):
        raise ExportError("No confirmed VBUS clear after RX removal")
    if (len(idle) != 1 or idle[0][1] != prior or not pg_on_us <= idle[0][0] < pg_off_us
            or idle[0][0] >= removed_ms * 1000):
        raise ExportError("Missing unambiguous NCI RF_DEACTIVATE idle confirmation while RX was online")

    on_pad = records[seated:removed]
    rx, battery, path = [], [], []
    for _, kind, body in on_pad:
        if kind == b"SAMPLE":
            values = dict(VALUE.findall(body))
            if values.get(b"supply") == b"rx1619" and values.get(b"online") == b"1":
                try:
                    voltage, current = (int(values[key]) for key in (b"voltage_now", b"current_now"))
                except (KeyError, ValueError):
                    continue
                if voltage > 0 and current >= 0:
                    rx.append({"voltage_uv": voltage, "current_ua": current})
            if values.get(b"supply") == b"qcom-battery":
                current = values.get(b"current_now", b"")
                if re.fullmatch(rb"-?\d+", current):
                    battery.append({"current_ua": int(current),
                                    "status": values.get(b"status", b"").decode("ascii", errors="replace")})
    for ms, body in cycle:
        if seated_ms <= ms <= removed_ms and b"miseed-power " in body:
            reading = re.search(rb"\bchg_enable=([01]) .*\bpower_path=0x([0-9a-f]{2})\b", body)
            if reading:
                if reading[1] == b"1":
                    raise ExportError("SMB5 reported charge enabled while RX was online")
                path.append(int(reading[2], 16))
    if not any(kind == b"TYPEC" and body == PARTNER_ABSENT for _, kind, body in on_pad):
        raise ExportError("No confirmed Type-C partner absence within the RX window")
    if any(kind == b"TYPEC" and body != PARTNER_ABSENT
           for _, kind, body in records[stages[1][0]:stages[-1][0]]):
        raise ExportError("Conflicting Type-C partner evidence after unplug")
    if not battery or not path:
        raise ExportError("Missing battery or PMIC observations within the RX window")
    if not rx:
        raise ExportError("No valid RX output telemetry within the RX window")
    # PM8150B POWER_PATH_STATUS bits from qcom_pm8150b_regs.h. An
    # input's selection bit is only evidence of a valid path with bit 0 set.
    paths = [{"raw": raw, "input_valid": bool(raw & 1),
              "usb_in": bool(raw & (1 << 4)), "dc_in": bool(raw & (1 << 3))}
             for raw in sorted(set(path))]
    return {"mode": mode.decode("ascii"), "rf_idle_previous": prior.decode("ascii"),
            "rf_idle_us": idle[0][0], "rx_pg_on_us": pg_on_us, "rx_pg_off_us": pg_off_us,
            "vbus_detected_while_seated": any(seated_ms <= us // 1000 <= removed_ms and vbus for us, vbus in cc),
            "rx_output": rx, "battery": battery, "pmic_power_path": paths}

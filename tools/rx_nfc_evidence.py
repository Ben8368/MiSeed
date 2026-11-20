"""Validate a retained MiSeed RX/NFC coexistence experiment."""

# SPDX-License-Identifier: AGPL-3.0-only

import re

from .collect_charge_log import BEGIN, END, ExportError


RECORD = re.compile(rb"seq=(\d+) boot_ms=(\d+) ([A-Z_]+) (.+)")
MILESTONES = (
    b"stage=detached online=0", b"stage=seated online=1",
    b"stage=blocked start_poll=ERFKILL", b"stage=removed online=0",
    b"stage=rediscovered targets=1 mifare=1",
)


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

    records = []
    for seq, line in enumerate(lines[1:-1], first):
        match = RECORD.fullmatch(line)
        if not match or int(match[1]) != seq:
            raise ExportError("Invalid charge-log record order")
        records.append((match[3], match[4]))

    mode = mode.encode("ascii")
    stages = [(i, body) for i, (kind, body) in enumerate(records) if kind == b"RX_NFC"]
    expected = (b"stage=armed mode=" + mode, *MILESTONES,
                b"stage=result mode=" + mode + b" at=rediscover rc=0 cleanup=0")
    if tuple(body for _, body in stages) != expected:
        raise ExportError("RX/NFC experiment incomplete or failed: " +
                          repr([body.decode("ascii", errors="replace") for _, body in stages]))

    detached, seated, _, removed, _, finished = [i for i, _ in stages[1:]]
    cycle = records[detached:finished + 1]
    observed_idle = [body.rsplit(b"=", 1)[-1] for kind, body in cycle
                     if kind == b"KERNEL" and b"miseed-rx-nfc rf_idle previous=" in body]
    prior = b"discovery" if mode == b"poll" else b"target"
    if observed_idle != [prior]:
        raise ExportError("Missing unambiguous NCI RF_DEACTIVATE idle confirmation")

    on_pad = records[seated:removed]
    rx, battery, path = [], [], []
    for kind, body in on_pad:
        if kind == b"SAMPLE":
            values = dict(token.split(b"=", 1) for token in body.split() if b"=" in token)
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
        elif kind == b"KERNEL" and b"miseed-power " in body:
            reading = re.search(rb"\bchg_enable=([01]) .*\bpower_path=0x([0-9a-f]{2})\b", body)
            if reading:
                if reading[1] == b"1":
                    raise ExportError("SMB5 reported charge enabled while RX was online")
                path.append(int(reading[2], 16))
    if not any(kind == b"TYPEC" and body == b"port=port0 partner=0 rc=0" for kind, body in on_pad):
        raise ExportError("No confirmed Type-C detach observation within the RX window")
    if any(kind == b"TYPEC" and body != b"port=port0 partner=0 rc=0" for kind, body in on_pad):
        raise ExportError("Conflicting Type-C evidence within the RX window")
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
            "rx_output": rx, "battery": battery, "pmic_power_path": paths}

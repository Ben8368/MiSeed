"""Offline GENI timing readback from a framed Umi KMSG export; no device access."""
# SPDX-License-Identifier: AGPL-3.0-only
import argparse
import json
from pathlib import Path
import re

if __package__:
    from .collect_display_evidence import validate_frame
else:
    from collect_display_evidence import validate_frame

PREFIX = rb'<[0-7]>\[\s*(\d+\.\d+)\] geni_i2c 984000\.i2c: I2C NACK bus=(\d+) addr=0x28 dir=write '
HEX = rb'0x([0-9a-f]{1,8})'
EVENT = re.compile(PREFIX + rb'phase=(address|data) gp_length=(\d+) irq=' + HEX +
                   rb'(?: clk_sel=' + HEX + rb' m_clk_cfg=' + HEX + rb' scl_counters=' + HEX + rb')?')
# Compare full register words, not just decoded fields; clk_sel is an index, not a rate.
TABLES = {(0x11, 0x202012): 'linux_19p2MHz_table', (0x21, 0x30140f): 'linux_32MHz_table',
          (0x11, 0x302412): 'android_source_table'}


def analyze(snapshot):
    frame = validate_frame(snapshot, b'KMSG')
    header = re.match(rb'KMSG_BEGIN\nKMSG_SNAPSHOT bytes=(\d+) ring_bytes=\d+ filtered=0 clear=0\n', frame)
    if not header:
        raise ValueError('missing leading KMSG snapshot header')
    events = []
    for line in frame[header.end():header.end() + int(header[1])].splitlines():
        if not re.match(PREFIX, line):
            continue
        event = EVENT.fullmatch(line)
        if not event:
            raise ValueError('incomplete or unrecognized GENI write NACK diagnostic')
        timestamp, bus, phase, length, irq, sel, cfg, counters = event.groups()
        if (phase == b'address') != (int(length) == 0):
            raise ValueError('inconsistent GENI phase and transfer length')
        if phase != b'address':
            continue
        row = dict(kernel_time_s=timestamp.decode(), bus=int(bus), irq=int(irq, 16), readback='NOT_RECORDED')
        if sel is not None:
            sel, cfg, counters = (int(v, 16) for v in (sel, cfg, counters))
            row.update(readback='RECORDED', clk_sel=sel, m_clk_cfg=hex(cfg), scl_counters=hex(counters),
                       clock_enabled=bool(cfg & 1), divider=(cfg >> 4) & 0xfff, high=(counters >> 20) & 1023,
                       low=(counters >> 10) & 1023, cycle=counters & 1023,
                       register_pair_matches=TABLES.get((cfg, counters), 'NO_KNOWN_MATCH'))
        events.append(row)
    return dict(address_nacks=events, ninth_clock='NOT_MEASURED', physical_scl_hz='NOT_MEASURED')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dmesg', type=Path)
    args = parser.parse_args()
    try:
        result = analyze(args.dmesg.read_bytes())
    except (OSError, ValueError) as exc:
        parser.exit(2, f'capture rejected: {exc}\n')
    print(json.dumps(result, indent=2))
    return 0 if any(e['readback'] == 'RECORDED' for e in result['address_nacks']) else 1


if __name__ == '__main__':
    raise SystemExit(main())

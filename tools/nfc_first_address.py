"""Decode only the first address from a synchronized digital capture, not NCI data."""
# SPDX-License-Identifier: AGPL-3.0-only
import argparse
import csv
import json
from pathlib import Path


def decode(stream):
    rows = csv.reader(stream, strict=True)
    fields = next(rows, [])
    base = ['time_ns', 'scl', 'sda', 'firm', 'ven', 'irq']
    if fields not in (base, base + ['clkreq']):
        raise ValueError('expected header ' + ','.join(base) + '[,clkreq]')
    # Bounded even for noisy captures. Times describe observed samples only.
    def new_window():
        return {name: {'changes': 0, 'last_change_ns': None} for name in fields[3:]}

    idle = new_window()
    address = new_window()
    previous = None
    start = None
    bits = []
    for i, row in enumerate(rows, 2):
        if i > 1_000_001:
            raise ValueError('crop capture to first transfer with preceding idle')
        if len(row) != len(fields) or any(x not in ('0', '1') for x in row[1:]):
            raise ValueError(f'invalid digital sample at row {i}')
        t, scl, sda, *control = map(int, row)
        levels = dict(zip(fields[3:], control))
        if previous is None:
            if t < 0 or (scl, sda) != (1, 1):
                raise ValueError('capture must begin with idle SCL/SDA and nonnegative time')
            initial = {'time_ns': t, **levels}
        else:
            last_t, last_scl, last_sda = previous
            if t <= last_t or (scl != last_scl and sda != last_sda):
                raise ValueError(f'unordered time or unresolved simultaneous bus edges at row {i}')
            # Include transitions on START in idle, and on the ninth rise in address.
            if len(bits) < 9:
                window = idle if start is None else address
                for name, value in levels.items():
                    if value != last_levels[name]:
                        window[name]['changes'] += 1
                        window[name]['last_change_ns'] = t
            if start is None:
                if scl != 1:
                    raise ValueError('clock edge before first START')
                if last_sda and not sda:
                    start = {'time_ns': t, **levels}
            elif scl == last_scl == 1 and sda != last_sda:
                raise ValueError('STOP or repeated START inside address/ACK')
            elif scl and not last_scl:
                bits.append(sda)
                if len(bits) == 9:
                    ack = {'time_ns': t, **levels}
            elif last_scl and not scl and len(bits) == 9:
                byte = sum(bit << (7 - n) for n, bit in enumerate(bits[:8]))
                return {'address_7bit': hex(byte >> 1), 'direction': 'read' if byte & 1 else 'write',
                        'ninth_clock': 'NACK' if bits[8] else 'ACK', 'sn100t_write': byte == 0x50,
                        'start': start, 'ninth_rising_edge': ack, 'initial': initial,
                        'control_to_start': idle, 'control_after_start_to_ninth': address}
        last_levels = levels
        previous = t, scl, sda
    raise ValueError('incomplete first address or ninth clock')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    args = parser.parse_args()
    try:
        with args.csv.open(encoding='utf-8-sig', newline='') as stream:
            result = decode(stream)
    except (ValueError, OSError, csv.Error) as exc:
        parser.exit(2, f'capture rejected: {exc}\n')
    print(json.dumps(result))
    return 0 if result['sn100t_write'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

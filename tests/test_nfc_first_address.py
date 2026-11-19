"""Synthetic digital captures only; these tests do not exercise a device."""
# SPDX-License-Identifier: AGPL-3.0-only
import io
import unittest

from tools.nfc_first_address import decode


def capture(byte=0x50, ack=0):
    bus = [(1, 1), (1, 0), (0, 0)]
    for bit in [int(c) for c in f'{byte:08b}'] + [ack]:
        bus.extend([(0, bit), (1, bit), (0, bit)])
    return 'time_ns,scl,sda,firm,ven,irq,clkreq\n' + ''.join(
        f'{n * 100},{c},{d},0,1,0,1\n' for n, (c, d) in enumerate(bus))


class FirstAddressTests(unittest.TestCase):
    def test_first_address_only(self):
        for byte, ack in ((0x50, 0), (0x50, 1), (0x51, 0), (0x48, 1)):
            result = decode(io.StringIO(capture(byte, ack) + 'not payload input\n'))
            self.assertEqual(result['address_7bit'], hex(byte >> 1))
            self.assertEqual(result['direction'], 'read' if byte & 1 else 'write')
            self.assertEqual(result['sn100t_write'], byte == 0x50)
            self.assertEqual(result['ninth_clock'], 'NACK' if ack else 'ACK')
            self.assertEqual(result['ninth_rising_edge']['clkreq'], 1)

    def test_control_pulses_with_identical_endpoint_levels(self):
        sample = capture().replace('100,1,0,', '50,1,1,1,1,0,1\n100,1,0,')
        sample = sample.replace('200,0,0,0,1,0,1', '200,0,0,0,0,0,0')
        # A change on the ninth rising edge counts; one after it does not.
        sample = sample.replace('2800,1,0,0,1,0,1', '2800,1,0,0,1,1,1\n2850,1,0,0,1,0,1')
        result = decode(io.StringIO(sample))
        self.assertEqual(result['initial']['time_ns'], 0)
        self.assertEqual(result['control_to_start']['firm'], {'changes': 2, 'last_change_ns': 100})
        window = result['control_after_start_to_ninth']
        for signal in ('clkreq', 'ven'):
            self.assertEqual(window[signal], {'changes': 2, 'last_change_ns': 300})
        self.assertEqual(window['irq'], {'changes': 1, 'last_change_ns': 2800})
        self.assertEqual(window['firm'], {'changes': 0, 'last_change_ns': None})

    def test_missing_optional_clkreq_is_not_a_low_sample(self):
        sample = '\n'.join(line.rsplit(',', 1)[0] for line in capture().splitlines())
        result = decode(io.StringIO(sample))
        for key in ('initial', 'start', 'ninth_rising_edge', 'control_to_start', 'control_after_start_to_ninth'):
            self.assertNotIn('clkreq', result[key])

    def test_rejects_ambiguous_captures(self):
        sample = capture()
        for bad in (sample.replace('100,1,0,', '100,0,0,'),
                    sample.replace('100,1,0,', '0,1,0,'),
                    sample.replace('100,1,0,', '100,1,X,'),
                    sample.replace('200,0,0,', '200,1,1,'),
                    sample.rsplit('\n', 2)[0], sample.replace('time_ns', 'time_s')):
            with self.assertRaises(ValueError):
                decode(io.StringIO(bad))

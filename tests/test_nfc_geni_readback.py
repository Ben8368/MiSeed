"""Synthetic readbacks only; do not infer the physical ninth clock."""
# SPDX-License-Identifier: AGPL-3.0-only
import unittest

from tools.nfc_geni_readback import analyze

LINE = b'<4>[ 7.071015] geni_i2c 984000.i2c: I2C NACK bus=1 addr=0x28 dir=write phase=address gp_length=0 irq=0x409'


def frame(*records):
    payload = b'\n'.join(records) + b'\n'
    return (b'KMSG_BEGIN\nKMSG_SNAPSHOT bytes=%d ring_bytes=4096 filtered=0 clear=0\n' % len(payload)
            + payload + b'\nKMSG_NOTE test\nKMSG_END\n')


class GeniReadbackTests(unittest.TestCase):
    def test_fields_and_exact_table_match(self):
        for cfg, counters, table, fields in (
                (0x11, 0x202012, 'linux_19p2MHz_table', (1, 2, 8, 18)),
                (0x21, 0x30140f, 'linux_32MHz_table', (2, 3, 5, 15)),
                (0x11, 0x302412, 'android_source_table', (1, 3, 9, 18)),
                (0x11, 0x80202012, 'NO_KNOWN_MATCH', (1, 2, 8, 18))):
            report = analyze(frame(LINE + b' clk_sel=0x1 m_clk_cfg=0x%x scl_counters=0x%x' % (cfg, counters)))
            event, = report['address_nacks']
            self.assertEqual(event['register_pair_matches'], table)
            self.assertEqual(tuple(event[k] for k in ('divider', 'high', 'low', 'cycle')), fields)
            self.assertEqual(report['ninth_clock'], 'NOT_MEASURED')

    def test_old_missing_foreign_or_multiple_events(self):
        other = LINE.replace(b'984000', b'988000')
        data = LINE.replace(b'address gp_length=0', b'data gp_length=1')
        self.assertEqual(analyze(frame(other, data) + LINE)['address_nacks'], [])
        events = analyze(frame(LINE, LINE))['address_nacks']
        self.assertEqual([r['readback'] for r in events], ['NOT_RECORDED'] * 2)

    def test_rejects_partial_readback_and_broken_framing(self):
        for broken in (frame(LINE + b' clk_sel=0x0'), frame(LINE).replace(b'KMSG_END', b''),
                       frame(LINE).replace(b'7.071015', b'7.01'), frame(LINE.replace(b'gp_length=0', b'gp_length=1'))):
            with self.assertRaises(ValueError):
                analyze(broken)

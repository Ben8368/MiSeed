"""Read-only NFC netlink fixtures and fail-closed source staging."""
# SPDX-License-Identifier: AGPL-3.0-only
import errno
import hashlib
import os
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from tools import apply_nfc_port as stage
from tools import nfc_inventory as nfc

ROOT = Path(__file__).resolve().parents[1]


def reply(kind, seq, body=b'', flags=0):
    length = 16 + len(body)
    return struct.pack('=IHHII', length, kind, flags, seq, 0) + body + bytes(nfc.align(length)-length)


def generic(kind, seq, payload):
    return reply(kind, seq, bytes([1, 1, 0, 0]) + payload)


class SocketFixture:
    def __init__(self, packets):
        self.packets = iter(packets)
        self.sent = []

    def sendto(self, data, peer):
        self.sent.append(data)
        assert peer == (0, 0)

    def recvmsg(self, size):
        return next(self.packets), [], 0, (0, 0)


class InventoryTests(unittest.TestCase):
    def family(self):
        return generic(16, 1, nfc.attribute(1, struct.pack('=H', 42)))

    def device(self):
        return (nfc.attribute(1, struct.pack('=I', 7)) + nfc.attribute(2, b'nfc7' + bytes(1)) +
                nfc.attribute(3, struct.pack('=I', 0x3e)) + nfc.attribute(12, bytes([1])))

    def test_standard_inventory_only_sends_get_commands(self):
        sock = SocketFixture([self.family(), generic(42, 2, self.device()) + reply(3, 2)])
        result = nfc.inventory(sock)
        self.assertEqual(result['devices'], [{'name': 'nfc7', 'index': 7, 'protocols': 62, 'powered': True}])
        self.assertEqual([packet[16] for packet in sock.sent], [3, 1])
        self.assertEqual(result['transport_rf_ndef_ese'], 'not tested')

    def test_empty_inventory_is_not_an_io_error(self):
        self.assertEqual(nfc.inventory(SocketFixture([self.family(), reply(3, 2)]))['devices'], [])

    def test_kernel_errors_propagate(self):
        for error in (errno.ENOENT, errno.EPERM, errno.EOPNOTSUPP):
            with self.subTest(error=error), self.assertRaises(OSError) as caught:
                nfc.inventory(SocketFixture([reply(2, 1, struct.pack('=i', -error))]))
            self.assertEqual(caught.exception.errno, error)

    def test_dump_error_and_interruption_not_reported_as_success(self):
        for packet in (reply(3, 2, struct.pack('=i', -errno.EIO)),
                       reply(3, 2, flags=nfc.NLM_F_DUMP_INTR), reply(4, 2)):
            with self.subTest(packet=packet), self.assertRaises(OSError):
                nfc.inventory(SocketFixture([self.family(), packet]))

    def test_malformed_messages_are_rejected(self):
        for packet in (b'x', reply(16, 99), reply(2, 1, b'x'),
                       generic(16, 1, b'xx'), generic(17, 1, b'')):
            with self.subTest(packet=packet), self.assertRaises(ValueError):
                nfc.inventory(SocketFixture([packet]))

    def test_attribute_lengths_duplicates_and_device_values(self):
        for payload in (b'x', struct.pack('=HH', 3, 1), nfc.attribute(1, b'a') * 2):
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                nfc.attributes(payload)
        valid = nfc.attributes(self.device())
        for key, value in ((1, b'xx'), (2, b'nfc0'), (2, bytes([97,0,98,0])), (12, bytes([2]))):
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                nfc.decode_device(valid | {key: value})

    def test_mutating_commands_rejected_before_send(self):
        sock = SocketFixture([])
        for command in (2, 3, 6, 7, 21, 27, 29):
            with self.subTest(command=command), self.assertRaises(ValueError):
                nfc.Netlink(sock).request(42, command)
        self.assertFalse(sock.sent)

    def test_timeout_propagates(self):
        sock = SocketFixture([])
        with patch.object(sock, 'recvmsg', side_effect=TimeoutError('timeout')):
            with self.assertRaises(TimeoutError):
                nfc.inventory(sock)


class StagingTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        for name in ('drivers/nfc', 'Documentation/devicetree/bindings/net/nfc'):
            (self.root / name).mkdir(parents=True)
        self.write('.miseed-source-revision', stage.REVISION)
        self.write('drivers/Kconfig', 'menu drivers' + chr(10) + 'endmenu' + chr(10))
        self.write('drivers/nfc/Kconfig', 'menu NFC' + chr(10) + 'endmenu' + chr(10))
        self.write('drivers/nfc/Makefile', '# upstream' + chr(10))

    def write(self, name, value):
        (self.root / name).write_text(value, encoding='utf-8')

    def apply(self):
        stage.apply(self.root, ROOT)

    def snapshot(self):
        return {str(p.relative_to(self.root)): p.read_bytes()
                for p in self.root.rglob('*') if p.is_file()}

    def test_idempotent_standard_nci_staging(self):
        self.apply()
        first = self.snapshot()
        self.apply()
        self.assertEqual(first, self.snapshot())
        self.assertIn(stage.KCONFIG_RULE, (self.root/'drivers/nfc/Kconfig').read_text())
        self.assertEqual((self.root/'drivers/nfc/sn100x.c').read_bytes(),
                         (ROOT/'kernel/nfc/sn100x.c').read_bytes())

    def test_known_legacy_cache_is_removed(self):
        self.write('drivers/Kconfig', 'endmenu' + chr(10) + stage.LEGACY_CONFIG)
        self.write('drivers/nfc/Makefile', 'obj-$(CONFIG_MISEED_NFC_NQ) += nq-nci.o' + chr(10))
        self.write('drivers/nfc/nq-nci.c', 'fixture')
        with patch.dict(stage.LEGACY_HASHES, {'nq-nci.c': hashlib.sha256(b'fixture').hexdigest()}):
            self.apply()
        self.assertFalse((self.root/'drivers/nfc/nq-nci.c').exists())
        self.assertNotIn('MISEED_NFC_NQ', (self.root/'drivers/Kconfig').read_text())

    def test_unknown_legacy_or_wrong_revision_fails_before_write(self):
        for path, value in (('drivers/nfc/nq-nci.c', 'changed'),
                            ('.miseed-source-revision', 'wrong')):
            self.write(path, value)
            before = self.snapshot()
            with self.assertRaises(ValueError):
                self.apply()
            self.assertEqual(before, self.snapshot())

    @unittest.skipIf(os.name == 'nt', 'POSIX symlink fixture')
    def test_output_symlink_is_rejected_without_writes(self):
        target = self.root / 'drivers/nfc/sn100x.c'
        target.symlink_to(self.root / 'drivers/Kconfig')
        before = self.snapshot()
        with self.assertRaises(ValueError):
            self.apply()
        self.assertEqual(before, self.snapshot())


if __name__ == '__main__':
    unittest.main()

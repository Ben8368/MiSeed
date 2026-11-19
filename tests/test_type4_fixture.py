"""Exercise the production read-only Type 4 consumer on Linux UNIX sockets."""
# SPDX-License-Identifier: AGPL-3.0-only
import shutil
import socket
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


@unittest.skipUnless(sys.platform == 'linux' and shutil.which('gcc'), 'Linux gcc/seqpacket required')
class Type4Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(tmp.cleanup)
        cls.program = Path(tmp.name) / 'fixture'
        source = '#include "rescue_type4.c"\nint main(void) { return type4_fixture(0) != 0; }\n'
        subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).resolve().parents[1]/'kernel'),
                        '-x', 'c', '-', '-o', str(cls.program)], input=source, text=True, check=True)

    def run_case(self, bad_at=None, replacement=None):
        sequence = [('00a4040007d276000085010100', ''), ('00a4000c02e103', ''),
                    ('00b000000f', '000f20003b00340406e104008000ff'), ('00a4000c02e104', ''),
                    ('00b0000002', '000d'), ('00b000020d', 'd101095402656e4d6953656564')]
        with self.subTest(bad_at=bad_at):
            host, child = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            with host, child, subprocess.Popen([self.program], stdin=child) as proc:
                child.close()
                host.settimeout(3)
                for i, (command, response) in enumerate(sequence):
                    self.assertEqual(host.recv(64).hex(), command)
                    host.sendall(bytes.fromhex(replacement if i == bad_at else '00'+response+'9000'))
                    if i == bad_at: break
                self.assertEqual(proc.wait(timeout=3), int(bad_at is not None))
                self.assertEqual(host.recv(64), b'')

    def test_read_fixture_and_stop_on_error(self):
        self.run_case()
        for step, response in [(0,'006a82'), (1,'019000'), (2,'00000f20003b00340406e1040080ffff9000'),
                               (3,'9000'), (4,'00000e9000'), (5,'00d101095402656e4d69536565659000')]:
            self.run_case(step, response)

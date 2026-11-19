"""Prevent missing fbdev capture ABI and native/header console-profile drift."""
# SPDX-License-Identifier: AGPL-3.0-only
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools import check_display_profile as profile
from tools import pack_android_boot_v2 as pack


class DisplayProfileTests(unittest.TestCase):
    def config(self, native):
        text = 'CONFIG_CMDLINE="' + pack.CMDLINE.decode() + '"\n'
        text += 'CONFIG_CMDLINE_FORCE=y\n'
        if native:
            text += '\n'.join('CONFIG_' + name + '=y' for name in (
                'FB_DEVICE',
                'FRAMEBUFFER_CONSOLE', 'DUMMY_CONSOLE', 'DRM_SIMPLEDRM',
                'DRM_PANEL_XIAOMI_UMI_CSOT', 'PRINTK_TIME', 'EVENT_TRACING')) + '\nCONFIG_LOG_BUF_SHIFT=20\n'
        return text

    def test_profile_requires_capture_and_rejects_failed_console_experiment(self):
        profile.check(self.config(False), False)
        profile.check(self.config(True), True)
        for option in ('FB_DEVICE',
                       'FRAMEBUFFER_CONSOLE', 'DUMMY_CONSOLE', 'DRM_SIMPLEDRM',
                       'DRM_PANEL_XIAOMI_UMI_CSOT', 'PRINTK_TIME', 'EVENT_TRACING'):
            with self.subTest(option=option), self.assertRaises(ValueError):
                profile.check(self.config(True).replace('CONFIG_' + option + '=y\n', ''), True)
        for old in ('CONFIG_CMDLINE_FORCE=y', 'CONFIG_LOG_BUF_SHIFT=20'):
            with self.assertRaises(ValueError):
                profile.check(self.config(True).replace(old, ''), True)
        for bad in (self.config(False), self.config(True) + 'CONFIG_FB_SIMPLE=y\n',
                    self.config(True) + 'CONFIG_FRAMEBUFFER_CONSOLE_DEFERRED_TAKEOVER=y\n',
                    self.config(True).replace('loglevel=8', 'loglevel=0')):
            with self.assertRaises(ValueError):
                profile.check(bad, True)
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            profile.check(self.config(True) + 'CONFIG_FB_DEVICE=n\n', True)

    def test_packed_header_uses_the_normal_console_profile(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            header = bytearray(pack.HEADER_SIZE)
            header[:8] = b'ANDROID!'
            struct.pack_into('<II', header, 36, pack.PAGE, 2)
            struct.pack_into('<I', header, 1644, pack.HEADER_SIZE)
            template = root / 'template'
            template.write_bytes(header)
            for name in ('kernel', 'ramdisk', 'dtb'):
                (root / name).write_bytes((name * 50).encode())
            for native in (False, True):
                output = root / ('native.img' if native else 'default.img')
                args = ['pack', '--template', str(template), '--template-sha256',
                        hashlib.sha256(header).hexdigest(), '--kernel', str(root / 'kernel'),
                        '--ramdisk', str(root / 'ramdisk'), '--dtb', str(root / 'dtb'),
                        '--output', str(output)]
                with patch.object(sys, 'argv', args):
                    self.assertEqual(pack.main(), 0)
                cmdline = output.read_bytes()[64:576].split(b'\0', 1)[0]
                self.assertEqual(cmdline, pack.CMDLINE)
                self.assertIn(b'loglevel=8', cmdline)

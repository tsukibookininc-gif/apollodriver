# SPDX-License-Identifier: GPL-2.0-only
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BINARY = Path(__file__).parents[1] / 'build/apollo-boot'


class BootCliTests(unittest.TestCase):
    def run_cli(self, *args):
        return subprocess.run([str(BINARY), *args], text=True, capture_output=True, timeout=5)

    def test_offline_synthetic_image_is_not_accepted_as_twin_x(self):
        # No UA bytes: a tiny constructed CY image with an independently known sum.
        data = b'CY\x1c\xb0' + struct.pack('<IIIIIII', 2, 0x100, 7, 9, 0, 0x100, 16)
        with tempfile.TemporaryDirectory() as folder:
            image = Path(folder) / 'ApolloTwinX.bin'
            image.write_bytes(data)
            result = self.run_cli('--image', str(image))
            self.assertEqual(result.returncode, 2, result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual(report['sha256'], hashlib.sha256(data).hexdigest())
            self.assertTrue(report['checksum_verified'])
            self.assertFalse(report['pinned_twin_x'])
            self.assertFalse(report['device_opened'])
            self.assertFalse(report['device_writes_performed'])
            self.assertFalse(report['hardware_audio_verified'])
            result = self.run_cli('--image', str(image), '--execute', '--location', '01100000', '--confirm-twin-x-ram-boot')
            self.assertEqual(result.returncode, 1)
            self.assertIn('not the pinned ApolloTwinX.bin; no device opened', result.stderr)
            result = self.run_cli('--image', str(image), '--write-verify-only', '--location', '01100000', '--confirm-twin-x-ram-write')
            self.assertEqual(result.returncode, 1)
            self.assertIn('not the pinned ApolloTwinX.bin; no device opened', result.stderr)

    def test_incomplete_execution_and_conflicting_flags_fail_before_file_open(self):
        for args in [[], ['--execute'], ['--confirm-twin-x-ram-boot'], ['--location', '0x01100000'],
                     ['--execute', '--location', '01100000'], ['--execute', '--confirm-twin-x-ram-boot'],
                     ['--plan', '--execute', '--location', '01100000', '--confirm-twin-x-ram-boot'],
                     ['--location', ' -0'], ['--execute', '--execute'],
                     ['--write-verify-only'], ['--confirm-twin-x-ram-write'],
                     ['--write-verify-only', '--location', '01100000', '--confirm-twin-x-ram-boot'],
                     ['--write-verify-only', '--confirm-twin-x-ram-write'],
                     ['--write-verify-only', '--plan', '--location', '01100000', '--confirm-twin-x-ram-write'],
                     ['--execute', '--write-verify-only', '--location', '01100000', '--confirm-twin-x-ram-boot']]:
            with self.subTest(args=args):
                result = self.run_cli('--image', 'nonexistent.bin', *args)
                self.assertEqual(result.returncode, 1)
                if args:
                    self.assertNotIn('cannot open firmware', result.stderr)

    def test_bounded_file_read_rejects_oversize_and_truncated_files(self):
        with tempfile.TemporaryDirectory() as folder:
            image = Path(folder) / 'input.bin'
            for data in [b'', b'CY\x1c\xb0', b'x' * (512 * 1024 + 1)]:
                image.write_bytes(data)
                result = self.run_cli('--image', str(image))
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, '')


if __name__ == '__main__':
    unittest.main()

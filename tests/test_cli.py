# SPDX-License-Identifier: GPL-2.0-only
import json
import platform
import subprocess
import unittest


class CliTests(unittest.TestCase):
    def run_cli(self, *args):
        return subprocess.run(["./build/apollo-inspect", *args], text=True, capture_output=True)

    def test_status_json(self):
        # Synthetic values, not a hardware observation.
        data = "4a4b2b2a0200000080bb000003000000160000000a000000000000000000000000000000"
        result = self.run_cli("--decode-status", data)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["sample_rate"], 48000)

    def test_malformed_input_fails_without_success_json(self):
        for args in [("--decode-config", "00"), ("--decode-status", "00"),
                     ("--location", "100000000"), ("--location", "-1"),
                     ("--location", "1oops"), ("--firmware", "anything"), ("--write",)]:
            with self.subTest(args=args):
                result = self.run_cli(*args)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")

    def test_linux_never_fakes_a_mac_device(self):
        if platform.system() == "Darwin":
            self.skipTest("Linux refusal test")
        result = self.run_cli()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires macOS", result.stderr)


if __name__ == "__main__":
    unittest.main()

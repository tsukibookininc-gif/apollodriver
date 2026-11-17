# SPDX-License-Identifier: GPL-2.0-only
import hashlib
import importlib.util
import json
from pathlib import Path
import tarfile
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("package_tool", Path(__file__).parents[1] / "tools/package.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PackageTests(unittest.TestCase):
    def test_archive_preserves_mode_and_checksums_match_archive_relative_paths(self):
        # Package the real locally compiled CLI, not a fabricated binary.
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "research.tar.gz"
            module.package(module.ROOT / "build/apollo-inspect", output, "a" * 40, "test-host", "test-arch")
            with tarfile.open(output) as archive:
                self.assertEqual(archive.getmember("apollo-inspect").mode, 0o755)
                self.assertEqual(len(archive.getmembers()), 5)
                sums = archive.extractfile("SHA256SUMS").read().decode().splitlines()
                for line in sums:
                    digest, name = line.split("  ")
                    self.assertNotIn("/", name)
                    self.assertEqual(hashlib.sha256(archive.extractfile(name).read()).hexdigest(), digest)
                info = json.load(archive.extractfile("BUILDINFO.json"))
                self.assertFalse(info["hardware_audio_verified"])
                self.assertIn("a" * 40, info["source_url"])

    def test_invalid_provenance_and_labels_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            for commit, platform in [("short", "macos-26"), ("a" * 40, "../../elsewhere")]:
                with self.assertRaises(ValueError):
                    module.package(module.ROOT / "build/apollo-inspect", Path(folder) / "test.tar.gz", commit, platform, "ARM64")

    def test_boot_archive_is_distinct_and_declares_write_capability(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "boot.tar.gz"
            module.package(module.ROOT / "build/apollo-boot", output, "a" * 40, "test-host", "test-arch", "apollo-boot")
            with tarfile.open(output) as archive:
                self.assertEqual(set(archive.getnames()), {"apollo-boot", "BOOT_EXPERIMENT.md", "LICENSE", "BUILDINFO.json", "SHA256SUMS"})
                self.assertEqual(archive.getmember("apollo-boot").mode, 0o755)
                self.assertIn(b"Copyright (c) 2017 okdshin", archive.extractfile("LICENSE").read())
                info = json.load(archive.extractfile("BUILDINFO.json"))
                self.assertTrue(info["device_writes_implemented"])
                self.assertEqual(info["default_mode"], "offline_plan")
                self.assertFalse(info["hardware_audio_verified"])


if __name__ == "__main__":
    unittest.main()

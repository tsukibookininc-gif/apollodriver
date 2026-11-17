# SPDX-License-Identifier: GPL-2.0-only
"""USB/UAC2 structural regression tests. All bytes are synthetic, not Apollo captures."""
import json
import struct
import subprocess
import unittest


def configuration(*records, interfaces=1):
    body = b"".join(records)
    return struct.pack("<BBHBBBBB", 9, 2, 9 + len(body), interfaces, 1, 0, 0x80, 50) + body


def interface(number=0, alternate=0, endpoints=0, subclass=2, protocol=0x20):
    return bytes([9, 4, number, alternate, endpoints, 1, subclass, protocol, 0])


EP = bytes.fromhex("07050305c00001")
COMPANION = bytes.fromhex("06300100c000")
AS_GENERAL = bytes.fromhex("10240101000101000000020000000000")
TYPE_I = bytes.fromhex("062402010418")  # four-byte subslot, 24 significant bits


class DescriptorTests(unittest.TestCase):
    def decode(self, data):
        return subprocess.run(["./build/apollo-inspect", "--decode-config", data.hex()], capture_output=True, text=True)

    def assert_rejected(self, data):
        result = self.decode(data)
        self.assertNotEqual(result.returncode, 0, "accepted inconsistent descriptors")
        self.assertEqual(result.stdout, "")

    def test_endpoint_count_mismatch(self):
        self.assert_rejected(configuration(interface(endpoints=0), EP))
        self.assert_rejected(configuration(interface(endpoints=1)))

    def test_interface_count_mismatch(self):
        self.assert_rejected(configuration(interface(), interfaces=2))

    def test_duplicate_interface_altset_or_endpoint(self):
        self.assert_rejected(configuration(interface(), interface()))
        self.assert_rejected(configuration(interface(endpoints=2), EP, EP))

    def test_missing_default_altset(self):
        self.assert_rejected(configuration(interface(alternate=1)))

    def test_orphan_companion(self):
        self.assert_rejected(configuration(interface(), COMPANION))

    def test_uac2_format_is_decoded_from_bytes(self):
        data = configuration(interface(), interface(alternate=1, endpoints=1), AS_GENERAL, TYPE_I, EP, COMPANION)
        result = self.decode(data)
        self.assertEqual(result.returncode, 0, result.stderr)
        records = json.loads(result.stdout)["records"]
        general = next(x for x in records if x.get("uac2_subtype") == "as_general")
        fmt = next(x for x in records if x.get("uac2_subtype") == "format_type_i")
        self.assertEqual(general["channels"], 2)
        self.assertEqual(general["format_bitmap"], "0x00000001")
        self.assertEqual(fmt["subslot_bytes"], 4)
        self.assertEqual(fmt["bit_resolution"], 24)

    def test_short_uac2_general_or_impossible_pcm_width(self):
        self.assert_rejected(configuration(interface(), bytes.fromhex("032401")))
        self.assert_rejected(configuration(interface(), bytes.fromhex("062402010121")))

    def test_uac1_descriptor_is_preserved_without_uac2_guess(self):
        uac1_general = bytes.fromhex("07240101010100")
        result = self.decode(configuration(interface(protocol=0), uac1_general))
        self.assertEqual(result.returncode, 0, result.stderr)
        record = json.loads(result.stdout)["records"][-1]
        self.assertEqual(record["raw_hex"], uac1_general.hex())
        self.assertNotIn("uac2_subtype", record)


if __name__ == "__main__":
    unittest.main()

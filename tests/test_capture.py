# SPDX-License-Identifier: GPL-2.0-only
import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location("analyze_init", Path(__file__).parents[1] / "tools/analyze_init.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def event(payload, flags=0, sequence=0, phase=0):
    packet = bytes([len(payload) // 4, flags, sequence, 0xDC]) + payload
    return module.EVENT.pack(10, phase, 0, 0, 0, 0, 0, 0, len(packet)) + packet


def capture(*events):
    return b"APLO2" + struct.pack("<I", len(events)) + b"".join(events)


class CaptureTests(unittest.TestCase):
    def test_command_split_over_frames_and_sequence_wrap(self):
        # Constructed QUERY_11 and CONNECT command words, not a UA capture.
        data = capture(event(struct.pack("<I", 0x00110002), sequence=255),
                       event(struct.pack("<III", 123, 0x00230002, 1), sequence=0))
        result = module.analyze(data)
        self.assertEqual(result["streams_by_flags"]["0x00"]["opcodes"], {"0x0011": 1, "0x0023": 1})
        self.assertFalse(result["replay_safe"])
        self.assertFalse(result["device_model_verified"])

    def test_flag_streams_are_separate(self):
        data = capture(event(struct.pack("<I", 0x00110002), flags=0),
                       event(struct.pack("<I", 0x00270001), flags=0x10),
                       event(struct.pack("<I", 123), flags=0))
        result = module.analyze(data)
        self.assertEqual(result["streams_by_flags"]["0x00"]["opcodes"], {"0x0011": 1})
        self.assertFalse(result["streams_by_flags"]["0x10"]["flags_semantics_verified"])

    def test_control_metadata_does_not_expose_payload(self):
        secret = b"private-firmware-bytes"
        record = module.EVENT.pack(99, 1, 1, 0x41, 3, 0, 0x062D, 0, len(secret)) + secret
        result = module.analyze(capture(record))
        self.assertEqual(result["control_out_events"], 1)
        self.assertNotIn(secret.decode(), str(result))

    def test_eeprom_write_is_flagged_but_never_replayed(self):
        result = module.analyze(capture(event(struct.pack("<10I", 0x0013000A, *([0] * 9)))))
        self.assertTrue(result["streams_by_flags"]["0x00"]["contains_eeprom_write_opcode"])
        self.assertFalse(result["replay_safe"])

    def test_all_truncations_fail(self):
        data = capture(event(struct.pack("<II", 0x00110002, 123)))
        for length in range(len(data)):
            with self.subTest(length=length), self.assertRaises(ValueError):
                module.analyze(data[:length])

    def test_bad_lengths_magic_kind_reserved_and_trailing_data_fail(self):
        good = capture(event(struct.pack("<II", 0x00110002, 123)))
        cases = [b"", good + b"extra", capture(event(struct.pack("<I", 0))),
                 capture(event(struct.pack("<I", 0x00010002)))]
        for index, value in [(0, 0), (9 + 6, 2), (9 + 9, 1), (9 + 16, 255), (9 + 19, 0xDD)]:
            mutated = bytearray(good)
            mutated[index] = value
            cases.append(mutated)
        for data in cases:
            with self.subTest(data=data), self.assertRaises(ValueError):
                module.analyze(data)


if __name__ == "__main__":
    unittest.main()

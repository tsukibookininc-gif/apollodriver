#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Offline-only APLO2 init analyzer; no USB imports, writes, or replay capability.

APLO2 layout and bulk framing: open-apollo tools/usb-re/pcap-init-extract.py
and tools/uausb/dspcmd.py; see docs/SOURCES.md. Only metadata and hashes are
printed. The input can contain proprietary DSP modules: keep it private.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct
import sys

MAX_BYTES = 64 * 1024 * 1024
EVENT = struct.Struct("<IHBBBBHHH")


def analyze(data):
    if len(data) > MAX_BYTES:
        raise ValueError("input exceeds 64 MiB limit")
    if len(data) < 9 or data[:5] != b"APLO2":
        raise ValueError("expected APLO2 sequence, not raw pcap or firmware")
    count, = struct.unpack_from("<I", data, 5)
    if count > (len(data) - 9) // EVENT.size:
        raise ValueError("event count exceeds available headers")
    offset = 9
    bulk_count = 0
    controls = collections.Counter()
    phases = collections.Counter()
    # Separate flag streams: Twin captures include flag 0x10 of unknown meaning.
    # Wordcounts are followed across frame boundaries, not guessed at each frame.
    streams = collections.defaultdict(list)
    duration_us = 0
    for _ in range(count):
        if len(data) - offset < EVENT.size:
            raise ValueError("truncated APLO2 event header")
        delta, phase, kind, request_type, request, reserved, value, index, size = EVENT.unpack_from(data, offset)
        offset += EVENT.size
        if size > len(data) - offset:
            raise ValueError("truncated APLO2 event payload")
        payload = data[offset:offset + size]
        offset += size
        if reserved != 0:
            raise ValueError("unknown APLO2 reserved field")
        duration_us += delta
        phases[phase] += 1
        if kind == 0:
            if len(payload) < 4 or payload[3] != 0xDC or len(payload) != 4 + payload[0] * 4:
                raise ValueError("invalid host-to-device bulk frame")
            if (request_type, request, value, index) != (0, 0, 0, 0):
                raise ValueError("bulk event has nonzero control fields")
            words = struct.unpack_from(f"<{payload[0]}I", payload, 4)
            streams[payload[1]].extend(words)
            bulk_count += 1
        elif kind == 1:
            if request_type not in (0x40, 0x41):
                raise ValueError("expected captured vendor control OUT (APLO2 v2)")
            # Aggregate setup fields, never print the OUT payload (may contain assets).
            controls[f"{request_type:02x}:{request:02x}:{value:04x}:{index:04x}:{size}"] += 1
        else:
            raise ValueError(f"unknown APLO2 event kind {kind}")
    if offset != len(data):
        raise ValueError("trailing bytes after declared APLO2 events")
    decoded = {}
    for flags, words in streams.items():
        cursor = 0
        opcodes = collections.Counter()
        while cursor < len(words):
            word = words[cursor]
            length = word & 0xFFFF
            if length == 0 or length > len(words) - cursor:
                raise ValueError(f"zero-length or truncated bulk command in flags {flags:#x}")
            opcodes[f"0x{word >> 16:04x}"] += 1
            cursor += length
        decoded[f"0x{flags:02x}"] = {
            "dwords": len(words),
            "opcodes": dict(sorted(opcodes.items())),
            "contains_eeprom_write_opcode": bool(opcodes.get("0x0013")),
            "flags_semantics_verified": flags == 0,
        }
    return {
        "format": "APLO2",
        "sha256": hashlib.sha256(data).hexdigest(),
        "events": count,
        "bulk_frames": bulk_count,
        "control_out_events": sum(controls.values()),
        "control_setups_type_request_value_index_length": dict(sorted(controls.items())),
        "phases": dict(sorted(phases.items())),
        "encoded_duration_us": duration_us,
        "streams_by_flags": decoded,
        "device_model_verified": False,  # APLO2 has no VID/PID/model/firmware identity.
        "replay_safe": False,
        "hardware_audio_verified": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, help="private APLO2 file from the owner's Windows USB capture")
    args = parser.parse_args()
    try:
        # Limit the read itself, not just the parser's subsequent allocation.
        with args.capture.open("rb") as source:
            data = source.read(MAX_BYTES + 1)
        print(json.dumps(analyze(data), indent=2))
    except (ValueError, OSError) as error:
        print(f"analyze_init: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

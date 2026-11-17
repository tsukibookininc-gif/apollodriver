#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build a research CLI archive in the cloud, preserving executable mode and provenance."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def package(binary, output, commit, platform, arch, tool="apollo-inspect"):
    if tool not in ("apollo-inspect", "apollo-boot"):
        raise ValueError("unknown tool")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("commit must be a complete Git SHA")
    for label in (platform, arch):
        if not re.fullmatch(r"[A-Za-z0-9._-]+", label):
            raise ValueError("invalid platform/architecture label")
    if not binary.is_file() or not binary.stat().st_mode & 0o111:
        raise ValueError("binary is missing or not executable")
    guide = "BOOT_EXPERIMENT.md" if tool == "apollo-boot" else "FIRST_TEST.md"
    license_text = (ROOT / "LICENSE").read_bytes()
    if tool == "apollo-boot":
        license_text += b"\n\nPicoSHA2 (non-macOS offline build):\n" + (ROOT / "third_party/PicoSHA2-LICENSE").read_bytes()
    files = {
        tool: binary.read_bytes(),
        "LICENSE": license_text,
        guide: (ROOT / "docs" / guide).read_bytes(),
        "BUILDINFO.json": (json.dumps({
            "commit": commit,
            "source_url": f"https://github.com/tsukibookininc-gif/apollodriver/tree/{commit}",
            "platform": platform,
            "architecture": arch,
            "signature": "ad-hoc; not Developer ID signed or notarized",
            "tool": tool,
            "device_writes_implemented": tool == "apollo-boot",
            "default_mode": "offline_plan" if tool == "apollo-boot" else "registry_only",
            "audio_streaming_implemented": False,
            "hardware_audio_verified": False,
        }, indent=2) + "\n").encode(),
    }
    files["SHA256SUMS"] = "".join(
        f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in sorted(files.items())
    ).encode()
    with tarfile.open(output, "w:gz") as archive:
        for name, data in sorted(files.items()):
            entry = tarfile.TarInfo(name)
            entry.size = len(data)
            entry.mode = 0o755 if name == tool else 0o644
            archive.addfile(entry, io.BytesIO(data))
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--arch", required=True)
    parser.add_argument("--tool", choices=("apollo-inspect", "apollo-boot"), default="apollo-inspect")
    args = parser.parse_args()
    try:
        result = package(ROOT / "build" / args.tool,
                         ROOT / f"build/{args.tool}-{args.platform}-{args.arch}.tar.gz",
                         args.commit, args.platform, args.arch, args.tool)
    except (OSError, ValueError) as error:
        parser.exit(1, f"package: {error}\n")
    print(result)


if __name__ == "__main__":
    main()

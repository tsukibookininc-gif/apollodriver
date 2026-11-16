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


def package(binary, output, commit, platform, arch):
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("commit must be a complete Git SHA")
    for label in (platform, arch):
        if not re.fullmatch(r"[A-Za-z0-9._-]+", label):
            raise ValueError("invalid platform/architecture label")
    if not binary.is_file() or not binary.stat().st_mode & 0o111:
        raise ValueError("binary is missing or not executable")
    files = {
        "apollo-inspect": binary.read_bytes(),
        "LICENSE": (ROOT / "LICENSE").read_bytes(),
        "FIRST_TEST.md": (ROOT / "docs/FIRST_TEST.md").read_bytes(),
        "BUILDINFO.json": (json.dumps({
            "commit": commit,
            "source_url": f"https://github.com/tsukibookininc-gif/apollodriver/tree/{commit}",
            "platform": platform,
            "architecture": arch,
            "signature": "ad-hoc; not Developer ID signed or notarized",
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
            entry.mode = 0o755 if name == "apollo-inspect" else 0o644
            archive.addfile(entry, io.BytesIO(data))
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--arch", required=True)
    args = parser.parse_args()
    try:
        result = package(ROOT / "build/apollo-inspect",
                         ROOT / f"build/apollo-inspect-{args.platform}-{args.arch}.tar.gz",
                         args.commit, args.platform, args.arch)
    except (OSError, ValueError) as error:
        parser.exit(1, f"package: {error}\n")
    print(result)


if __name__ == "__main__":
    main()

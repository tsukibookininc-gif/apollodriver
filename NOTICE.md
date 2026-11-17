# Notice and provenance

This independent research implementation is licensed under GPL-2.0-only.

The USB IDs, interface-0 request numbers, status layout, DSP boot-word address and
ready rule are derived from published protocol documentation and code in
rolotrealanis98/open-apollo, licensed GPL-2.0, by rolotrealanis98 and contributors.
The native C++ transport and parsers in this repository are newly written. They
do not contain UA driver source or firmware. See docs/SOURCES.md for pinned sources.

The LICENSE file is the standard GNU GPL v2 license text, copied from open-apollo.
API declarations are used through Apple's installed macOS SDK; no Apple SDK or
header is redistributed. Synthetic test bytes are constructed for parser tests.
They are not captured Apollo descriptors or evidence of device support.

Universal Audio, Apollo and UAD are marks of Universal Audio. macOS and Apple Silicon
are marks of Apple. No affiliation or endorsement is claimed. Firmware/DSP assets
come only from the owner's own lawful UAD installation. They are not fetched from
third-party repositories or redistributed by this project. USB capture files may
contain those assets and should remain private.

Owner-supplied firmware attachments are inspected privately in the cloud; they are
not committed to Git, included in Actions artifacts or used as public test fixtures.
Published hashes, sizes and section addresses are metadata, not firmware payloads.

The non-macOS offline SHA-256 backend vendors PicoSHA2 by okdshin under the MIT
license; see third_party/README.md and third_party/PicoSHA2-LICENSE. The header
retains its original copyright/license notice. The macOS backend uses CommonCrypto.

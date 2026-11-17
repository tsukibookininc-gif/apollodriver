# Apollo Twin X USB on macOS

Experimental, native macOS USB driver research for **Apollo Twin X USB** on Apple Silicon.
This is an early USB transport implementation, **not a working Core Audio sound driver**.
It does not make the device appear in UA Connect or UAD Console. No audio success is claimed.

Разработка ведётся в облаке. Владелец самостоятельно скачал утилиту и выполнил
проверку IORegistry и cached descriptors; системные компоненты проекта не устанавливались.
Цель — сначала подтвердить USB-протокол именно Twin X USB, затем воспроизведение,
после него запись. Работоспособность звука на M5 пока **не подтверждена**.

## Implemented

- Real macOS IORegistry discovery, restricted to UA `2b5a:000e` (FX3 loader)
  and `2b5a:000f` (Twin X USB runtime USB identity).
- Cached configuration descriptor inspection: interfaces, alternate settings,
  endpoint direction/type/packet sizes, SuperSpeed companions and original bytes.
- UAC2 channel/format/subslot decoding and consistency checks for interface and
  endpoint counts, duplicate alternate settings and orphan companions.
- Native IOKit/IOUSBLib vendor-interface transport with an exact allowlist of
  **five device-to-host reads**, bounded timeouts, no seize, and stop on first error.
- Protocol/status/version/DSP boot-word decoding with malformed-reply rejection.
- Actual macOS version/process architecture and existing Core Audio device inventory.
- Offline APLO2 init-capture analyzer: control metadata, bulk command counts,
  cross-frame parsing, SHA-256 and EEPROM-opcode detection, with no replay function.
  Observed sequence gaps/resets withhold command summaries instead of splicing
  unknown data. This cannot establish that the original capture is complete.
- Offline descriptor/status decoders and protocol tests. Tests use synthetic data,
  not an attached Apollo and not proof of audio functionality.
- Separate experimental `apollo-boot`: offline CY image/checksum/SHA256 plan by
  default; explicit confirmed execution can write only the pinned Twin X image to
  FX3 RAM, verify every chunk by readback and observe re-enumeration. Hardware
  execution has not been approved or tested. See [the experiment](docs/BOOT_EXPERIMENT.md).

The default scan only reads OS properties; it does not open the USB interface or
send USB commands. `--descriptors` reads the OS-cached descriptors through IOUSBLib
without opening the device. `--status` additionally opens vendor interface 0 and
issues known IN requests. Even read-only vendor commands remain unverified on Twin X
and are **not part of the first experiment**.

## Build in the cloud

GitHub Actions builds and tests on Linux, macOS 15 arm64, and macOS 26 arm64.
The macOS jobs compile the real Apple backend and save an ad-hoc signed research
CLI `.tar.gz` artifact with executable mode, archive-relative SHA256SUMS,
source commit metadata, license and [first-test instructions](docs/FIRST_TEST.md).
There is no installer, system extension, daemon or background service.
The RAM boot tool is packaged separately; the diagnostic archive does not contain it.
Cloud CI has **no Apollo attached**. Its success cannot verify USB transfers or audio.

```sh
make -j2 test
make analyze
```

Native macOS builds need Apple's Command Line Tools and system frameworks only;
there is no Homebrew/libusb/Python dependency in the native USB executable.
Linux builds provide the offline decoders and intentionally refuse live macOS scans.
macOS boot hashing uses built-in CommonCrypto. Non-macOS offline builds use the
vendored MIT-licensed PicoSHA2 header; no extra package installation is required.

Examples of offline use (HEX is data supplied by the researcher):

```sh
build/apollo-inspect --decode-config HEX
build/apollo-inspect --decode-status HEX
build/apollo-inspect --inspect-config HEX
python3 tools/analyze_init.py private/twin-x-init.seq
```

## First owner-run hardware observation

The owner supplied screenshots of successful default and cached-descriptor scans
on the M5 Mac. The device reports `2b5a:000e`, `WestBridge`, one vendor-class
interface and no additional endpoints, consistent with the FX3 loader stage.
There are no audio interfaces in this configuration and no Apollo in the reported
Core Audio inventory. This verifies discovery and cached inspection only;
runtime USB, vendor transfers, firmware/DSP initialization and audio remain unverified.
No firmware or DSP data has been sent by this tool.
See [the research report](docs/RESEARCH.md) for evidence, limits and the next experiment.
See also [hardware evidence](docs/HARDWARE.md) and [the code audit](docs/AUDIT.md).
The [detection and registration note](docs/DETECTION.md) records the observations,
the boot-image source needed next, and the distinction between USB discovery,
Console recognition and UA registration. No new download is needed for this result.
The owner subsequently ran the offline boot plan successfully and requested a
safer next step. [Offline USB-table analysis](docs/OFFLINE_USB.md) identifies two
configuration candidates inside the private image without executing it or touching
hardware. RAM writes and firmware execution remain deferred and unapproved.

Live-mode exit `0` means USB inspection completed, **not** that Apollo was found
or audio works: inspect `devices` and error fields. Exit `2` and
`usb_diagnostics_ok: false` mean requested descriptor/status work failed, including
status requested from a loader. Exit `1` means an argument, discovery or selection
error. Core Audio inventory errors are reported separately from the USB result.
Strict offline decoders return `1` for malformed input. `--inspect-config` preserves
the raw configuration and a parse error as JSON, with exit `2` when invalid.
The macOS cached-descriptor mode also preserves parse errors and bytes; combined
descriptor/status mode does not issue status reads after descriptor validation fails.

The read-only `apollo-inspect` has no firmware uploader. The separate, opt-in
RAM boot experiment is limited to a pinned Twin X image; it is not a flash/EEPROM
programmer. No DSP initializer, arbitrary request sender or audio stream is included.
The Solo initialization must not be replayed on Twin X as a shortcut.
Any future device write, installation or security change needs a separate review
and the owner's agreement. Do not disable Gatekeeper/SIP or lower boot security.

## Sources and licensing

Based on published protocol facts from
[open-apollo](https://github.com/rolotrealanis98/open-apollo) and Apple's USB/Core Audio
documentation; the exact references are in [SOURCES.md](docs/SOURCES.md).
GPL-2.0-only; see [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md).
No Universal Audio firmware, binaries, artwork, captures or DSP modules are included.
Independent project, not affiliated with Universal Audio or Apple.

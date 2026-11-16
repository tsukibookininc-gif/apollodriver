# Apollo Twin X USB on macOS

Experimental, native macOS USB driver research for **Apollo Twin X USB** on Apple Silicon.
This is an early USB transport implementation, **not a working Core Audio sound driver**.
It does not make the device appear in UA Connect or UAD Console. No audio success is claimed.

Разработка ведётся в облаке. На Mac владельца ничего не установлено и не запущено.
Цель — сначала подтвердить USB-протокол именно Twin X USB, затем воспроизведение,
после него запись. Работоспособность звука на M5 пока **не подтверждена**.

## Implemented

- Real macOS IORegistry discovery, restricted to UA `2b5a:000e` (FX3 loader)
  and `2b5a:000f` (Twin X USB runtime USB identity).
- Cached configuration descriptor inspection: interfaces, alternate settings,
  endpoint direction/type/packet sizes, SuperSpeed companions and original bytes.
- Native IOKit/IOUSBLib vendor-interface transport with an exact allowlist of
  **five device-to-host reads**, bounded timeouts, no seize, and stop on first error.
- Protocol/status/version/DSP boot-word decoding with malformed-reply rejection.
- Actual macOS version/process architecture and existing Core Audio device inventory.
- Offline APLO2 init-capture analyzer: control metadata, bulk command counts,
  cross-frame parsing, SHA-256 and EEPROM-opcode detection, with no replay function.
- Offline descriptor/status decoders and protocol tests. Tests use synthetic data,
  not an attached Apollo and not proof of audio functionality.

The default scan only reads OS properties; it does not open the USB interface or
send USB commands. `--descriptors` reads the OS-cached descriptors through IOUSBLib
without opening the device. `--status` additionally opens vendor interface 0 and
issues known IN requests. Even read-only vendor commands remain unverified on Twin X
and are **not part of the first experiment**.

## Build in the cloud

GitHub Actions builds and tests on Linux, macOS 15 arm64, and macOS 26 arm64.
The macOS jobs compile the real Apple backend and save an ad-hoc signed research
CLI artifact. There is no installer, system extension, daemon or background service.
Cloud CI has **no Apollo attached**. Its success cannot verify USB transfers or audio.

```sh
make -j2 test
```

Native macOS builds need Apple's Command Line Tools and system frameworks only;
there is no Homebrew/libusb/Python dependency in the native USB executable.
Linux builds provide the offline decoders and intentionally refuse live macOS scans.

Examples of offline use (HEX is data supplied by the researcher):

```sh
build/apollo-inspect --decode-config HEX
build/apollo-inspect --decode-status HEX
python3 tools/analyze_init.py private/twin-x-init.seq
```

## Hardware testing is pending

No command has been run on the owner's Mac. No firmware or DSP data has been sent.
First collect macOS's existing System Information USB view; then, only when the
owner chooses to run the tool, use the default scan and cached descriptors.
See [the research report](docs/RESEARCH.md) for evidence, limits and the next experiment.

No firmware uploader, DSP initializer, arbitrary request sender or audio stream is
included. The Solo initialization must not be replayed on Twin X as a shortcut.
Any future device write, installation or security change needs a separate review
and the owner's agreement. Do not disable Gatekeeper/SIP or lower boot security.

## Sources and licensing

Based on published protocol facts from
[open-apollo](https://github.com/rolotrealanis98/open-apollo) and Apple's USB/Core Audio
documentation; the exact references are in [SOURCES.md](docs/SOURCES.md).
GPL-2.0-only; see [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md).
No Universal Audio firmware, binaries, artwork, captures or DSP modules are included.
Independent project, not affiliated with Universal Audio or Apple.

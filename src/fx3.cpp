// SPDX-License-Identifier: GPL-2.0-only
#include "apollo/fx3.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace apollo {
namespace {
bool ram_range(std::uint32_t address, std::size_t length) {
    const auto end = std::uint64_t(address) + length;
    // Conservative 256 KiB SRAM envelope; exclude bootloader/descriptor RAM.
    return length && !(address & 3) && !(length & 3) &&
        ((address < 0x4000 && end <= 0x4000) ||
         (address >= 0x40003000 && end <= 0x40040000));
}
Fx3Request request(std::uint8_t type, std::uint32_t address, std::size_t size) {
    return {type, 0xa0, static_cast<std::uint16_t>(address),
            static_cast<std::uint16_t>(address >> 16), static_cast<std::uint16_t>(size)};
}
}  // namespace

Bytes read_fx3_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open firmware file for reading");
    Bytes data(fx3_max_image + 1);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    const auto count = file.gcount();
    if (file.bad() || (file.fail() && !file.eof())) throw std::runtime_error("firmware file read failed");
    if (count < 0 || static_cast<std::size_t>(count) > fx3_max_image)
        throw std::invalid_argument("firmware file exceeds 512 KiB limit");
    data.resize(static_cast<std::size_t>(count));
    return data;
}

Fx3Image parse_fx3(const Bytes& data) {
    if (data.size() < 20 || data.size() > fx3_max_image || data.size() % 4)
        throw std::invalid_argument("invalid FX3 image length");
    if (data[0] != 'C' || data[1] != 'Y' || data[3] != 0xb0)
        throw std::invalid_argument("only CY/B0 checksum images are supported");
    if ((data[2] & 0xc1) || (data[2] & 0x30) == 0x30)
        throw std::invalid_argument("reserved image bits/clock setting or non-executable image");
    Fx3Image image;
    image.control = data.at(2);
    std::size_t offset = 4;
    std::uint32_t checksum = 0;
    for (;;) {
        const auto words = le32(data, offset), address = le32(data, offset + 4);
        offset += 8;
        if (!words) {
            if (image.sections.empty() || data.size() - offset != 4)
                throw std::invalid_argument("missing sections or invalid FX3 terminator/checksum trailer");
            image.checksum = le32(data, offset);
            if (image.checksum != checksum) throw std::invalid_argument("FX3 checksum mismatch");
            image.entrypoint = address;
            if (address & 3) throw std::invalid_argument("unaligned FX3 entrypoint");
            const bool contained = std::any_of(image.sections.begin(), image.sections.end(), [&](const auto& s) {
                return address >= s.address && std::uint64_t(address) + 4 <= std::uint64_t(s.address) + s.length;
            });
            if (!contained) throw std::invalid_argument("FX3 entrypoint is outside loaded sections");
            return image;
        }
        // No byte-length fallback, allocation from untrusted word counts, MMIO,
        // descriptor area, DTCM, ROM, flash/EEPROM writes or overlapping segments.
        if (words > (data.size() - offset) / 4)
            throw std::invalid_argument("truncated FX3 section payload");
        const std::size_t length = std::size_t(words) * 4;
        if (!ram_range(address, length)) throw std::invalid_argument("FX3 section outside permitted RAM");
        for (const auto& s : image.sections)
            if (address < std::uint64_t(s.address) + s.length && s.address < std::uint64_t(address) + length)
                throw std::invalid_argument("overlapping FX3 sections");
        for (std::size_t i = 0; i < length; i += 4) checksum += le32(data, offset + i);
        image.sections.push_back({address, offset, length});
        if (image.sections.size() > 64) throw std::invalid_argument("too many FX3 sections");
        offset += length;
    }
}

void require_twin_x_image(const Bytes& data) {
    // Content identity, not filename. A renamed Solo/Twin image is refused.
    if (data.size() != 147416 || sha256(data) != twin_x_image_sha256)
        throw std::invalid_argument("not the pinned ApolloTwinX.bin; no device opened");
    const auto image = parse_fx3(data);
    if (image.control != 0x1c || image.entrypoint != 0x40011820 || image.sections.size() != 4)
        throw std::invalid_argument("unexpected pinned Twin X image layout");
}

std::string fx3_plan(const Bytes& data) {
    const auto image = parse_fx3(data);
    const auto digest = sha256(data);
    std::size_t count = 0, total = 0;
    std::ostringstream out;
    out << "{\"schema_version\":1,\"mode\":\"offline_ram_boot_plan\",\"device_opened\":false"
        << ",\"size\":" << data.size() << ",\"sha256\":" << quote(digest)
        << ",\"pinned_twin_x\":" << (data.size() == 147416 && digest == twin_x_image_sha256 ? "true" : "false")
        << ",\"image_control\":" << quote(hex(image.control, 2))
        << ",\"image_checksum\":" << quote(hex(image.checksum, 8))
        << ",\"checksum_verified\":true,\"entrypoint\":" << quote(hex(image.entrypoint, 8))
        << ",\"sections\":[";
    for (std::size_t i = 0; i < image.sections.size(); ++i) {
        const auto& s = image.sections[i];
        if (i) out << ',';
        out << "{\"address\":" << quote(hex(s.address, 8)) << ",\"offset\":" << s.offset
            << ",\"length\":" << s.length << '}';
        total += s.length;
        count += (s.length + fx3_chunk_size - 1) / fx3_chunk_size;
    }
    out << "],\"ram_bytes\":" << total << ",\"planned_data_writes\":" << count
        << ",\"planned_readbacks\":" << count << ",\"max_chunk_bytes\":4096"
        << ",\"request\":\"A0\",\"write_type\":\"40\",\"readback_type\":\"C0\""
        << ",\"planned_jump_writes\":1,\"device_writes_performed\":false"
        << ",\"runtime_observed\":false,\"audio_streaming_implemented\":false,\"hardware_audio_verified\":false}";
    return out.str();
}

bool fx3_request_allowed(const Fx3Request& r, const Fx3Image& image, bool allow_jump) {
    const auto address = std::uint32_t(r.value) | (std::uint32_t(r.index) << 16);
    if ((r.type != 0x40 && r.type != 0xc0) || r.request != 0xa0 || r.length > fx3_chunk_size) return false;
    if (!r.length) return allow_jump && r.type == 0x40 && address == image.entrypoint;
    if ((address & 3) || (r.length & 3)) return false;
    return std::any_of(image.sections.begin(), image.sections.end(), [&](const auto& s) {
        return address >= s.address && std::uint64_t(address) + r.length <= std::uint64_t(s.address) + s.length;
    });
}

void transfer_fx3_ram(const Bytes& data, const Fx3Control& control, Fx3Progress& progress, bool execute_entrypoint) {
    // Parse the entire file and checksum before the first callback. Pinning is
    // also mandatory at the real macOS entry point; synthetic tests exercise
    // this sequencing function without pretending their bytes are UA firmware.
    progress = {};
    const auto image = parse_fx3(data);
    for (const auto& section : image.sections) {
        for (std::size_t sent = 0; sent < section.length; sent += fx3_chunk_size) {
            const auto size = std::min(fx3_chunk_size, section.length - sent);
            const auto address = section.address + static_cast<std::uint32_t>(sent);
            const auto start = data.begin() + static_cast<std::ptrdiff_t>(section.offset + sent);
            const Bytes payload(start, start + static_cast<std::ptrdiff_t>(size));
            ++progress.writes_attempted;
            auto written = control(request(0x40, address, size), payload);
            if (written.transferred != size || !written.data.empty()) throw std::runtime_error("short/invalid RAM write reply");
            ++progress.writes_completed;
            auto read = control(request(0xc0, address, size), {});
            if (read.transferred != size || read.data != payload) throw std::runtime_error("RAM readback mismatch or short reply");
            ++progress.readbacks_verified;
        }
    }
    if (!execute_entrypoint) return;
    progress.jump_attempted = true;
    const auto jumped = control(request(0x40, image.entrypoint, 0), {});
    if (jumped.transferred || !jumped.data.empty()) throw std::runtime_error("invalid jump reply");
    progress.jump_acknowledged = true;
}

}  // namespace apollo

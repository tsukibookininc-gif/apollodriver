// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace apollo {
using Bytes = std::vector<std::uint8_t>;
constexpr std::uint16_t vendor_id = 0x2b5a;
constexpr std::uint16_t twin_x_loader = 0x000e;
constexpr std::uint16_t twin_x_runtime = 0x000f;

std::string quote(const std::string& value);
std::string hex(std::uint64_t value, unsigned width = 0);
std::string hex_bytes(const Bytes& data);
Bytes unhex(const std::string& text);
std::uint32_t parse_location(const std::string& text);
std::uint16_t le16(const Bytes& data, std::size_t offset);
std::uint32_t le32(const Bytes& data, std::size_t offset);

// No caller-supplied control request. All supported requests are device-to-host,
// interface 0, and have exact lengths from open-apollo's documented request map.
enum class Read { protocol, status, version_a, version_b, dsp0_boot };
struct ReadRequest {
    std::uint8_t type;
    std::uint8_t request;
    std::uint16_t value;
    std::uint16_t index;
    std::uint16_t length;
};
ReadRequest request_for(Read kind);
bool allowed(const ReadRequest& request);
std::string read_name(Read kind);
std::string decode_read(Read kind, const Bytes& data);
struct ReadSequenceResult {
    std::string json;
    bool complete;
};
// Shared fault handling, also exercised offline with explicitly synthetic replies.
ReadSequenceResult read_known_status(const std::function<Bytes(Read)>& transport);

// Strict, offline descriptor parser. Unknown/class-specific records stay raw.
// Endpoint addresses, altsets and channel counts are never guessed from Solo.
std::string decode_configuration(const Bytes& data);
struct ConfigurationEvidence {
    std::string json;
    bool valid;
};
// Preserve malformed cached bytes for diagnosis without treating them as valid.
ConfigurationEvidence configuration_evidence(const Bytes& data);
}  // namespace apollo


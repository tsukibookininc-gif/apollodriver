// SPDX-License-Identifier: GPL-2.0-only
#include "apollo/protocol.hpp"

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace apollo {
std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
            else out << c;
        }
    }
    out << '"';
    return out.str();
}

std::string hex(std::uint64_t value, unsigned width) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setfill('0') << std::setw(static_cast<int>(width)) << value;
    return out.str();
}

std::string hex_bytes(const Bytes& data) {
    std::ostringstream out;
    for (auto b : data) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(b);
    return out.str();
}

Bytes unhex(const std::string& text) {
    Bytes out;
    int high = -1;
    for (unsigned char c : text) {
        if (c == ' ' || c == '\n' || c == '\t' || c == '\r') continue;
        int n;
        if (c >= '0' && c <= '9') n = c - '0';
        else if (c >= 'a' && c <= 'f') n = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') n = c - 'A' + 10;
        else throw std::invalid_argument("invalid hex digit (omit 0x prefixes)");
        if (high < 0) high = n;
        else { out.push_back(static_cast<std::uint8_t>((high << 4) | n)); high = -1; }
    }
    if (high >= 0) throw std::invalid_argument("odd number of hex digits");
    return out;
}

std::uint16_t le16(const Bytes& data, std::size_t offset) {
    if (offset > data.size() || data.size() - offset < 2) throw std::invalid_argument("truncated u16");
    return std::uint16_t(data[offset]) | (std::uint16_t(data[offset + 1]) << 8);
}

std::uint32_t parse_location(const std::string& text) {
    std::size_t first = text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0 ? 2 : 0;
    if (text.size() <= first || text.size() - first > 8)
        throw std::invalid_argument("invalid 32-bit hexadecimal location");
    std::uint32_t result = 0;
    for (std::size_t i = first; i < text.size(); ++i) {
        unsigned char c = text[i];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else throw std::invalid_argument("invalid 32-bit hexadecimal location");
        result = (result << 4) | digit;
    }
    return result;
}

std::uint32_t le32(const Bytes& data, std::size_t offset) {
    if (offset > data.size() || data.size() - offset < 4) throw std::invalid_argument("truncated u32");
    return std::uint32_t(data[offset]) | (std::uint32_t(data[offset + 1]) << 8) |
           (std::uint32_t(data[offset + 2]) << 16) | (std::uint32_t(data[offset + 3]) << 24);
}

ReadRequest request_for(Read kind) {
    switch (kind) {
    case Read::protocol: return {0xc1, 0x00, 0, 0, 4};
    case Read::status: return {0xc1, 0x06, 0, 0, 36};
    case Read::version_a: return {0xc1, 0x0e, 0, 0, 4};
    case Read::version_b: return {0xc1, 0x0f, 0, 0, 4};
    case Read::dsp0_boot: return {0xc1, 0x04, 0x1069, 0, 4};
    }
    throw std::invalid_argument("unknown read kind");
}

bool allowed(const ReadRequest& r) {
    for (auto kind : {Read::protocol, Read::status, Read::version_a, Read::version_b, Read::dsp0_boot}) {
        auto good = request_for(kind);
        if (r.type == good.type && r.request == good.request && r.value == good.value &&
            r.index == good.index && r.length == good.length) return true;
    }
    return false;
}

std::string read_name(Read kind) {
    switch (kind) {
    case Read::protocol: return "protocol";
    case Read::status: return "status";
    case Read::version_a: return "version_a";
    case Read::version_b: return "version_b";
    case Read::dsp0_boot: return "dsp0_boot";
    }
    throw std::invalid_argument("unknown read kind");
}

std::string decode_read(Read kind, const Bytes& data) {
    if (data.size() != request_for(kind).length) throw std::invalid_argument("short or oversized vendor reply");
    auto w = le32(data, 0);
    std::ostringstream out;
    if (kind == Read::status) {
        if (w != 0x2a2b4b4a) throw std::invalid_argument("status magic mismatch; protocol not established");
        out << "{\"rate_index\":" << le32(data, 4) << ",\"sample_rate\":" << le32(data, 8)
            << ",\"clock_source_count\":" << le32(data, 12)
            << ",\"capture_channels_reported\":" << le32(data, 16)
            << ",\"playback_channels_reported\":" << le32(data, 20)
            << ",\"trailing_words\":[" << le32(data, 24) << ',' << le32(data, 28) << ',' << le32(data, 32) << "]}";
    } else {
        out << "{\"word\":" << quote(hex(w, 8));
        if (kind == Read::dsp0_boot) {
            bool started = w != 0 && w != 0xf81a3c5e;
            out << ",\"started_by_upstream_rule\":" << (started ? "true" : "false")
                << ",\"ready_bit_by_upstream_rule\":" << (started && (w & 1) ? "true" : "false");
        }
        out << '}';
    }
    return out.str();
}

ReadSequenceResult read_known_status(const std::function<Bytes(Read)>& transport) {
    std::ostringstream out;
    bool first = true;
    bool complete = true;
    out << '{';
    for (auto kind : {Read::protocol, Read::status, Read::version_a, Read::version_b, Read::dsp0_boot}) {
        if (!first) out << ',';
        first = false;
        out << quote(read_name(kind)) << ':';
        try {
            auto raw = transport(kind);
            auto decoded = decode_read(kind, raw);
            out << "{\"raw_hex\":" << quote(hex_bytes(raw)) << ",\"decoded\":" << decoded << '}';
        } catch (const std::exception& error) {
            complete = false;
            out << "{\"error\":" << quote(error.what()) << "},\"stopped_after_error\":true";
            break;
        }
    }
    out << '}';
    return {out.str(), complete};
}

std::string decode_configuration(const Bytes& data) {
    if (data.size() < 9 || data[0] != 9 || data[1] != 2) throw std::invalid_argument("invalid configuration header");
    auto total = le16(data, 2);
    if (total != data.size()) throw std::invalid_argument("configuration total length mismatch");
    std::ostringstream out;
    out << "{\"value\":" << unsigned(data[5]) << ",\"interface_count\":" << unsigned(data[4])
        << ",\"total_length\":" << total << ",\"raw_hex\":" << quote(hex_bytes(data)) << ",\"records\":[";
    bool first = true;
    int interface = -1, alternate = -1;
    unsigned endpoint_count = 0, expected_endpoints = 0;
    unsigned interface_class = 0, subclass = 0, protocol = 0;
    unsigned previous_type = 0, endpoint_address = 0;
    std::set<unsigned> interfaces, defaults, endpoints;
    std::set<std::pair<unsigned, unsigned>> alternate_settings;
    auto finish_interface = [&] {
        if (interface >= 0 && endpoint_count != expected_endpoints)
            throw std::invalid_argument("interface endpoint count mismatch");
    };
    for (std::size_t p = 0; p < data.size();) {
        if (data.size() - p < 2) throw std::invalid_argument("truncated descriptor header");
        auto len = data[p];
        auto type = data[p + 1];
        if (len < 2 || len > data.size() - p) throw std::invalid_argument("invalid descriptor length");
        if (!first) out << ',';
        first = false;
        Bytes raw(data.begin() + p, data.begin() + p + len);
        out << "{\"offset\":" << p << ",\"type\":" << unsigned(type)
            << ",\"raw_hex\":" << quote(hex_bytes(raw));
        if (type == 4) {
            if (len < 9) throw std::invalid_argument("short interface descriptor");
            finish_interface();
            interface = data[p + 2]; alternate = data[p + 3];
            if (!alternate_settings.emplace(interface, alternate).second)
                throw std::invalid_argument("duplicate interface alternate setting");
            interfaces.insert(interface);
            if (alternate == 0) defaults.insert(interface);
            endpoint_count = 0;
            expected_endpoints = data[p + 4];
            endpoints.clear();
            interface_class = data[p + 5]; subclass = data[p + 6]; protocol = data[p + 7];
            out << ",\"interface\":" << interface << ",\"alternate\":" << alternate
                << ",\"endpoint_count\":" << unsigned(data[p + 4])
                << ",\"class\":" << unsigned(data[p + 5])
                << ",\"subclass\":" << unsigned(data[p + 6])
                << ",\"protocol\":" << unsigned(data[p + 7]);
        } else if (type == 5) {
            if (len < 7 || interface < 0) throw std::invalid_argument("short or orphan endpoint descriptor");
            auto address = data[p + 2];
            if (!(address & 0x0f) || (address & 0x70) || !endpoints.insert(address).second)
                throw std::invalid_argument("invalid or duplicate endpoint address");
            ++endpoint_count;
            endpoint_address = address;
            unsigned attributes = data[p + 3];
            static const char* types[] = {"control", "isochronous", "bulk", "interrupt"};
            out << ",\"interface\":" << interface << ",\"alternate\":" << alternate
                << ",\"address\":" << quote(hex(address, 2))
                << ",\"direction\":" << quote(address & 0x80 ? "in" : "out")
                << ",\"transfer\":" << quote(types[attributes & 3])
                << ",\"sync_type\":" << ((attributes >> 2) & 3)
                << ",\"usage_type\":" << ((attributes >> 4) & 3)
                << ",\"max_packet_size_raw\":" << le16(data, p + 4)
                << ",\"interval\":" << unsigned(data[p + 6]);
        } else if (type == 48) {
            if (len < 6 || previous_type != 5) throw std::invalid_argument("short or orphan SuperSpeed endpoint companion");
            out << ",\"interface\":" << interface << ",\"alternate\":" << alternate
                << ",\"endpoint_address\":" << quote(hex(endpoint_address, 2))
                << ",\"max_burst\":" << unsigned(data[p + 2])
                << ",\"attributes\":" << unsigned(data[p + 3])
                << ",\"bytes_per_interval\":" << le16(data, p + 4);
        } else if (type == 0x24 && interface_class == 1 && protocol == 0x20) {
            if (len < 3) throw std::invalid_argument("short UAC2 class-specific descriptor");
            unsigned subtype = data[p + 2];
            if (subclass == 2 && subtype == 1) {
                if (len != 16) throw std::invalid_argument("invalid UAC2 AS_GENERAL length");
                out << ",\"uac2_subtype\":\"as_general\",\"terminal_link\":" << unsigned(data[p + 3])
                    << ",\"format_type\":" << unsigned(data[p + 5])
                    << ",\"format_bitmap\":" << quote(hex(le32(data, p + 6), 8))
                    << ",\"channels\":" << unsigned(data[p + 10])
                    << ",\"channel_config\":" << quote(hex(le32(data, p + 11), 8));
            } else if (subclass == 2 && subtype == 2) {
                if (len < 4) throw std::invalid_argument("short UAC2 format descriptor");
                if (data[p + 3] == 1) {
                    if (len != 6 || data[p + 4] < 1 || data[p + 4] > 4 ||
                        data[p + 5] == 0 || data[p + 5] > 8 * data[p + 4])
                        throw std::invalid_argument("invalid UAC2 Type I sample width");
                    out << ",\"uac2_subtype\":\"format_type_i\",\"subslot_bytes\":" << unsigned(data[p + 4])
                        << ",\"bit_resolution\":" << unsigned(data[p + 5]);
                }
            } else if (subclass == 1 && subtype == 0x0a) {
                if (len != 8) throw std::invalid_argument("invalid UAC2 clock-source length");
                out << ",\"uac2_subtype\":\"clock_source\",\"clock_id\":" << unsigned(data[p + 3])
                    << ",\"clock_attributes\":" << unsigned(data[p + 4])
                    << ",\"clock_controls\":" << unsigned(data[p + 5]);
            }
        } else if (type == 2 && p != 0) {
            throw std::invalid_argument("nested configuration descriptor");
        }
        out << '}';
        previous_type = type;
        p += len;
    }
    finish_interface();
    if (interfaces.size() != data[4]) throw std::invalid_argument("configuration interface count mismatch");
    if (defaults != interfaces) throw std::invalid_argument("interface missing default alternate setting 0");
    out << "]}";
    return out.str();
}

ConfigurationEvidence configuration_evidence(const Bytes& data) {
    try {
        return {decode_configuration(data), true};
    } catch (const std::invalid_argument& error) {
        std::ostringstream out;
        out << "{\"raw_hex\":" << quote(hex_bytes(data)) << ",\"parse_error\":" << quote(error.what()) << '}';
        return {out.str(), false};
    }
}
}  // namespace apollo


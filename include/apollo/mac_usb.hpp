// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace apollo {
struct ScanOptions {
    bool descriptors = false;
    bool status = false;
    std::optional<std::uint32_t> location;
};
// Returns JSON; errors remain errors and never become fabricated device data.
std::string inspect_mac(const ScanOptions& options);
}  // namespace apollo


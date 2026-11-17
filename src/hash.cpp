// SPDX-License-Identifier: GPL-2.0-only
#include "apollo/fx3.hpp"
#include <array>
#include <limits>
#include <stdexcept>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include "../third_party/picosha2.h"
#endif

namespace apollo {
std::string sha256(const Bytes& data) {
    std::array<unsigned char, 32> digest{};
#ifdef __APPLE__
    if (data.size() > std::numeric_limits<CC_LONG>::max())
        throw std::invalid_argument("input too large for CommonCrypto");
    if (!CC_SHA256(data.data(), static_cast<CC_LONG>(data.size()), digest.data()))
        throw std::runtime_error("CommonCrypto SHA256 failed");
#else
    picosha2::hash256(data.begin(), data.end(), digest.begin(), digest.end());
#endif
    return hex_bytes(Bytes(digest.begin(), digest.end()));
}
}  // namespace apollo

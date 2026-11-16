// SPDX-License-Identifier: GPL-2.0-only
#include "apollo/protocol.hpp"

#include <functional>
#include <iostream>
#include <random>
#include <stdexcept>

namespace {
unsigned checks = 0;
void require(bool condition) {
    ++checks;
    if (!condition) throw std::runtime_error("test assertion failed at check " + std::to_string(checks));
}
void rejects(const std::function<void()>& action) {
    bool caught = false;
    try { action(); } catch (const std::invalid_argument&) { caught = true; }
    require(caught);
}
}

int main() {
    using namespace apollo;
    try {
        require(quote("\"\n\\\t\x01") == "\"\\\"\\n\\\\\\t\\u0001\"");
        require(unhex("ab CD\n00") == Bytes({0xab, 0xcd, 0}));
        rejects([] { unhex("abc"); });
        rejects([] { unhex("0x12"); });
        rejects([] { le32({}, 0); });
        rejects([] { le32({1, 2, 3, 4}, SIZE_MAX); });
        for (auto kind : {Read::protocol, Read::status, Read::version_a, Read::version_b, Read::dsp0_boot}) {
            auto request = request_for(kind);
            require(allowed(request));
            require(request.type == 0xc1 && request.index == 0);
            for (unsigned type = 0; type < 256; ++type) {
                auto bad = request;
                bad.type = static_cast<std::uint8_t>(type);
                require(allowed(bad) == (type == 0xc1));
            }
            auto bad = request;
            ++bad.length; require(!allowed(bad));
            bad = request; ++bad.index; require(!allowed(bad));
            bad = request; ++bad.value; require(!allowed(bad));
            for (unsigned n = 0; n < request.length; ++n)
                rejects([=] { decode_read(kind, Bytes(n, 0)); });
        }
        require(decode_read(Read::dsp0_boot, unhex("5e3c1af8")).find("\"started_by_upstream_rule\":false") != std::string::npos);
        require(decode_read(Read::dsp0_boot, unhex("030c0000")).find("\"ready_bit_by_upstream_rule\":true") != std::string::npos);
        auto status = unhex("4a4b2b2a0200000080bb000003000000160000000a000000000000000000000000000000");
        require(decode_read(Read::status, status).find("\"sample_rate\":48000") != std::string::npos);
        status[0] = 0;
        rejects([&] { decode_read(Read::status, status); });
        // Synthetic configuration, NOT a captured Apollo descriptor.
        auto config = unhex("09021f00010100803209040201010102000007050305c0000106300100c000");
        auto parsed = decode_configuration(config);
        require(parsed.find("\"address\":\"0x03\"") != std::string::npos);
        require(parsed.find("\"transfer\":\"isochronous\"") != std::string::npos);
        require(parsed.find("\"bytes_per_interval\":192") != std::string::npos);
        for (std::size_t n = 0; n < config.size(); ++n)
            rejects([&] { decode_configuration(Bytes(config.begin(), config.begin() + n)); });
        auto bad = config; bad[9] = 0;
        rejects([&] { decode_configuration(bad); });
        bad = config; bad[18] = 255;
        rejects([&] { decode_configuration(bad); });
        bad = config; bad[10] = 0x24;  // orphan endpoint
        rejects([&] { decode_configuration(bad); });
        // Exercise untrusted lengths under ASan/UBSan; successful parses are not hardware evidence.
        std::mt19937 rng(0x2b5a000f);
        for (unsigned i = 0; i < 10000; ++i) {
            Bytes input(rng() % 100);
            for (auto& byte : input) byte = static_cast<std::uint8_t>(rng());
            if (input.size() >= 9 && i % 2 == 0) {
                input[0] = 9; input[1] = 2;
                input[2] = static_cast<std::uint8_t>(input.size()); input[3] = 0;
            }
            try { decode_configuration(input); } catch (const std::invalid_argument&) {}
        }
        std::cout << checks << " protocol checks passed; 10000 malformed-input cases exercised.\n"
                     "Synthetic/offline tests only; hardware audio is not tested.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

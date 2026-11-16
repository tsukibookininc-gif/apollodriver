// SPDX-License-Identifier: GPL-2.0-only
#include "apollo/protocol.hpp"
#include "apollo/mac_usb.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        apollo::ScanOptions options;
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << "Apollo Twin X USB research transport (no audio streaming yet)\n"
                         "  apollo-inspect [--descriptors] [--status] [--location HEX]\n"
                         "  apollo-inspect --decode-config HEX\n"
                         "  apollo-inspect --decode-status HEX\n"
                         "Default: IORegistry + Core Audio inventory; no device opens or USB commands.\n"
                         "--descriptors: read OS-cached configuration descriptors, without opening USB.\n"
                         "--status: open vendor interface 0 without seize; five known IN requests.\n"
                         "No firmware, DSP init, bulk writes, resets or system installation.\n";
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--decode-config") {
            std::cout << apollo::decode_configuration(apollo::unhex(argv[2])) << '\n';
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--decode-status") {
            std::cout << apollo::decode_read(apollo::Read::status, apollo::unhex(argv[2])) << '\n';
            return 0;
        }
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--descriptors") options.descriptors = true;
            else if (arg == "--status") options.status = true;
            else if (arg == "--location" && i + 1 < argc) {
                std::string text = argv[++i];
                std::size_t n = 0;
                auto location = std::stoull(text, &n, 16);
                if (text.empty() || text[0] == '-' || n != text.size() || location > std::numeric_limits<std::uint32_t>::max())
                    throw std::invalid_argument("invalid 32-bit hexadecimal location");
                options.location = static_cast<std::uint32_t>(location);
            } else throw std::invalid_argument("unknown/incomplete argument: " + arg);
        }
#ifdef __APPLE__
        auto result = apollo::inspect_mac(options);
        std::cout << result.json << '\n';
        return result.usb_diagnostics_ok ? 0 : 2;
#else
        throw std::runtime_error("live USB backend requires macOS; offline decoders work on this OS");
#endif
    } catch (const std::exception& e) {
        std::cerr << "apollo-inspect: " << e.what() << '\n';
        return 1;
    }
}

// SPDX-License-Identifier: GPL-2.0-only
#include "apollo/fx3.hpp"
#include <iostream>
#include <optional>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << "Apollo Twin X USB experimental RAM boot (no audio driver)\n"
                "  apollo-boot --image PATH [--plan]\n"
                "Default: offline file inspection and plan; no USB access.\n"
                "  apollo-boot --image PATH --execute --location HEX --confirm-twin-x-ram-boot\n"
                "EXECUTE writes pinned firmware to FX3 RAM, reads it back, then jumps.\n"
                "  apollo-boot --image PATH --write-verify-only --location HEX --confirm-twin-x-ram-write\n"
                "WRITE-VERIFY-ONLY writes and reads RAM; no entrypoint jump. Still requires approval.\n"
                "Separate owner agreement is required before a hardware experiment.\n"
                "No EEPROM/flash programmer, DSP initialization, audio or installer.\n";
            return 0;
        }
        std::string path;
        std::optional<std::uint32_t> location;
        bool execute = false, confirm = false, plan = false, verify_only = false, confirm_write = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--image" && i + 1 < argc && path.empty()) path = argv[++i];
            else if (arg == "--location" && i + 1 < argc && !location) location = apollo::parse_location(argv[++i]);
            else if (arg == "--execute" && !execute) execute = true;
            else if (arg == "--confirm-twin-x-ram-boot" && !confirm) confirm = true;
            else if (arg == "--plan" && !plan) plan = true;
            else if (arg == "--write-verify-only" && !verify_only) verify_only = true;
            else if (arg == "--confirm-twin-x-ram-write" && !confirm_write) confirm_write = true;
            else throw std::invalid_argument("unknown, repeated or incomplete argument; use --help");
        }
        if (path.empty()) throw std::invalid_argument("--image PATH is required; use --help");
        if (execute && (plan || verify_only || confirm_write || !confirm || !location))
            throw std::invalid_argument("execution requires --location and --confirm-twin-x-ram-boot; --plan cannot execute");
        if (verify_only && (plan || execute || confirm || !confirm_write || !location))
            throw std::invalid_argument("RAM write/verify requires --location and --confirm-twin-x-ram-write; no boot confirmation");
        if (!execute && !verify_only && (confirm || confirm_write || location))
            throw std::invalid_argument("confirmation/location are execution-only; offline plan needs only --image");
        const auto data = apollo::read_fx3_file(path);
        if (!execute && !verify_only) {
            std::cout << apollo::fx3_plan(data) << '\n';
            return data.size() == 147416 && apollo::sha256(data) == apollo::twin_x_image_sha256 ? 0 : 2;
        }
        // Validate every byte before calling any platform discovery/open method.
        apollo::require_twin_x_image(data);
#ifdef __APPLE__
        bool complete = false;
        std::cout << apollo::boot_mac(data, *location, execute, complete) << '\n';
        return complete ? 0 : 2;
#else
        throw std::runtime_error("execution requires macOS; this host supports offline plans only");
#endif
    } catch (const std::exception& e) {
        std::cerr << "apollo-boot: " << e.what() << '\n';
        return 1;
    }
}

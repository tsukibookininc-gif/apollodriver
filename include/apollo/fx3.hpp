// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "apollo/protocol.hpp"

#include <functional>

namespace apollo {
constexpr std::size_t fx3_max_image = 512 * 1024;
constexpr std::size_t fx3_chunk_size = 4096;
constexpr const char* twin_x_image_sha256 =
    "bdd9db3f2d139f615c4f1e4e0261d24d90a6f95f8c6e1afe34e0c1a8fbd35fcc";

struct Fx3Section { std::uint32_t address = 0; std::size_t offset = 0, length = 0; };
struct Fx3Image {
    std::uint8_t control = 0;
    std::uint32_t checksum = 0, entrypoint = 0;
    std::vector<Fx3Section> sections;
};
std::string sha256(const Bytes& data);
Bytes read_fx3_file(const std::string& path);
// A deliberately narrow RAM-only subset of AN76405, not a general flash parser.
Fx3Image parse_fx3(const Bytes& data);
void require_twin_x_image(const Bytes& data);
std::string fx3_plan(const Bytes& data);

struct Fx3Request {
    std::uint8_t type = 0x40, request = 0xa0;
    std::uint16_t value = 0, index = 0, length = 0;
};
struct Fx3Reply { std::size_t transferred; Bytes data; };
bool fx3_request_allowed(const Fx3Request& request, const Fx3Image& image, bool allow_jump);
using Fx3Control = std::function<Fx3Reply(const Fx3Request&, const Bytes&)>;
struct Fx3Progress {
    std::size_t writes_attempted = 0, writes_completed = 0, readbacks_verified = 0;
    bool jump_attempted = false, jump_acknowledged = false;
};
// Injectable sequencing only. Native execution separately requires the pinned
// Twin X image and explicit command-line confirmation before opening any device.
void transfer_fx3_ram(const Bytes& data, const Fx3Control& control, Fx3Progress& progress, bool execute_entrypoint);

// macOS implementation; observes runtime identity, never claims working audio.
std::string boot_mac(const Bytes& data, std::uint32_t location, bool execute_entrypoint, bool& stage_complete);
}  // namespace apollo

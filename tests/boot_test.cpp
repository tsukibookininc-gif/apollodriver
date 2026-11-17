// SPDX-License-Identifier: GPL-2.0-only
#include "apollo/fx3.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>

namespace {
unsigned checks = 0;
void expect(bool condition) { ++checks; if (!condition) throw std::runtime_error("boot test failed"); }
void put(apollo::Bytes& b, std::uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(n >> (i * 8)));
}
void change(apollo::Bytes& b, std::size_t offset, std::uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) b.at(offset + i) = static_cast<std::uint8_t>(n >> (i * 8));
}
apollo::Bytes fixture(const std::vector<std::pair<std::uint32_t, std::size_t>>& sections = {{0x100, 20}, {0x4000fff0, 8212}}) {
    // Constructed pattern, not UA firmware. Crosses both a 4096-byte chunk and
    // the 16-bit address boundary to check wValue/wIndex carry independently.
    apollo::Bytes b{'C','Y',0x1c,0xb0};
    std::uint32_t sum = 0, word = 1;
    for (const auto& [address, length] : sections) {
        put(b, static_cast<std::uint32_t>(length / 4)); put(b, address);
        for (std::size_t i = 0; i < length / 4; ++i) { put(b, word); sum += word++; }
    }
    put(b, 0); put(b, sections.front().first); put(b, sum);
    return b;
}
template<class F> void rejects(F f) {
    bool failed = false;
    try { f(); } catch (const std::exception&) { failed = true; }
    expect(failed);
}
struct SyntheticRam {
    std::map<std::uint32_t, std::uint8_t> ram;
    std::vector<apollo::Fx3Request> calls;
    unsigned fail_at = 0;
    bool short_write = false, short_read = false, corrupt_read = false;
    apollo::Fx3Reply operator()(const apollo::Fx3Request& r, const apollo::Bytes& payload) {
        calls.push_back(r);
        expect(r.request == 0xa0 && r.length <= 4096);
        if (fail_at == calls.size()) throw std::runtime_error("injected transfer failure");
        const auto address = std::uint32_t(r.value) | (std::uint32_t(r.index) << 16);
        if (!r.length) { expect(r.type == 0x40 && payload.empty()); return {0, {}}; }
        if (r.type == 0x40) {
            expect(payload.size() == r.length);
            for (std::size_t i = 0; i < payload.size(); ++i) ram[address + static_cast<std::uint32_t>(i)] = payload[i];
            return {std::size_t(r.length) - (short_write ? 1 : 0), {}};
        }
        expect(r.type == 0xc0 && payload.empty());
        apollo::Bytes b;
        for (unsigned i = 0; i < r.length; ++i) b.push_back(ram.at(address + i));
        if (corrupt_read) b[0] ^= 1;
        if (short_read) b.pop_back();
        return {b.size(), b};
    }
};
}  // namespace

int main() {
    try {
        // Published SHA-256 test vectors; exercise each host's crypto library.
        expect(apollo::sha256({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        expect(apollo::sha256({'a','b','c'}) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        expect(apollo::sha256(apollo::Bytes(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
        const auto good = fixture();
        const auto image = apollo::parse_fx3(good);
        expect(image.sections.size() == 2 && image.entrypoint == 0x100);
        const apollo::Fx3Request jump{0x40, 0xa0, 0x100, 0, 0};
        expect(apollo::fx3_request_allowed(jump, image, true));
        expect(!apollo::fx3_request_allowed(jump, image, false));
        expect(!apollo::fx3_request_allowed({0xc0, 0xa0, 0x100, 0, 0}, image, true));
        expect(!apollo::fx3_request_allowed({0x40, 0xa0, 0x104, 0, 0}, image, true));
        expect(!apollo::fx3_request_allowed({0x40, 0xa2, 0x100, 0, 4}, image, false));
        expect(apollo::fx3_plan(good).find("\"pinned_twin_x\":false") != std::string::npos);
        rejects([&] { apollo::require_twin_x_image(good); });
        rejects([&] { apollo::require_twin_x_image(apollo::Bytes(147416, 0)); });
        SyntheticRam ram;
        apollo::Fx3Progress progress;
        apollo::transfer_fx3_ram(good, [&](const auto& r, const auto& b) { return ram(r, b); }, progress, true);
        expect(progress.writes_completed == 4 && progress.readbacks_verified == 4 && progress.jump_acknowledged);
        expect(ram.calls.size() == 9 && ram.calls[4].value == 0x0ff0 && ram.calls[4].index == 0x4001);
        SyntheticRam verify_only;
        apollo::transfer_fx3_ram(good, [&](const auto& r, const auto& b) {
            expect(r.length > 0); // No zero-length jump can reach the transport.
            return verify_only(r, b);
        }, progress, false);
        expect(verify_only.calls.size() == 8 && progress.writes_completed == 4);
        expect(progress.readbacks_verified == 4 && !progress.jump_attempted && !progress.jump_acknowledged);
        for (unsigned failure = 1; failure <= 9; ++failure) {
            SyntheticRam faulty; faulty.fail_at = failure;
            rejects([&] { apollo::transfer_fx3_ram(good, [&](const auto& r, const auto& b) { return faulty(r, b); }, progress, true); });
            expect(faulty.calls.size() == failure && !progress.jump_acknowledged);
            expect(progress.jump_attempted == (failure == 9));
        }
        for (unsigned mode = 0; mode < 3; ++mode) {
            SyntheticRam faulty;
            faulty.short_write = mode == 0; faulty.short_read = mode == 1; faulty.corrupt_read = mode == 2;
            rejects([&] { apollo::transfer_fx3_ram(good, [&](const auto& r, const auto& b) { return faulty(r, b); }, progress, true); });
            expect(!progress.jump_attempted && faulty.calls.size() == (mode == 0 ? 1 : 2));
        }
        for (std::size_t n = 0; n < good.size(); ++n) {
            rejects([&] { apollo::parse_fx3(apollo::Bytes(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(n))); });
        }
        for (const auto& [offset, value] : std::vector<std::pair<std::size_t, std::uint32_t>>{
            {0, 0}, {4, 0xffffffff}, {8, 0x10000000}, {8, 0x40000010}, {8, 0x101},
            {8, 0xfffffffc}, {36, 0x100}, {good.size()-8, 0x40030000}, {good.size()-4, 0}}) {
            auto bad = good; change(bad, offset, value);
            unsigned callbacks = 0;
            rejects([&] { apollo::transfer_fx3_ram(bad, [&](const auto&, const auto&) {
                ++callbacks; return apollo::Fx3Reply{0, {}};
            }, progress, true); });
            expect(callbacks == 0);
            expect(progress.writes_attempted == 0 && !progress.jump_attempted);
        }
        auto bad = good; bad.push_back(0);
        rejects([&] { apollo::parse_fx3(bad); });
        for (auto control : {std::uint8_t(0x1d), std::uint8_t(0x9c), std::uint8_t(0x3c)}) {
            bad = good; bad[2] = control; rejects([&] { apollo::parse_fx3(bad); });
        }
        auto overlap = fixture({{0x100, 8}, {0x104, 8}});
        rejects([&] { apollo::parse_fx3(overlap); });
        std::vector<std::pair<std::uint32_t, std::size_t>> many;
        for (unsigned i = 0; i < 65; ++i) many.emplace_back(0x100 + 4 * i, 4);
        rejects([&] { apollo::parse_fx3(fixture(many)); });
        std::mt19937 random(0xf3);
        for (unsigned i = 0; i < 10000; ++i) {
            apollo::Bytes fuzz;
            if (i % 2) {
                fuzz = good;
                fuzz[random() % fuzz.size()] ^= static_cast<std::uint8_t>(1 + random() % 255);
            } else {
                fuzz.resize(random() % 1024);
                for (auto& byte : fuzz) byte = static_cast<std::uint8_t>(random());
            }
            try { apollo::parse_fx3(fuzz); } catch (const std::exception&) {}
        }
        std::cout << checks << " boot checks and 10000 malformed-input cases passed (synthetic transport, no USB)\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}

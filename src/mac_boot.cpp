// SPDX-License-Identifier: GPL-2.0-only
#include "apollo/fx3.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <algorithm>
#include <chrono>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace apollo {
namespace {
struct Object {
    io_object_t value;
    explicit Object(io_object_t v) : value(v) {}
    ~Object() { if (value) IOObjectRelease(value); }
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&& other) noexcept : value(std::exchange(other.value, IO_OBJECT_NULL)) {}
};
void check(IOReturn status, const char* operation) {
    if (status != kIOReturnSuccess) {
        std::ostringstream error;
        error << operation << ": IOKit " << hex(static_cast<std::uint32_t>(status), 8);
        throw std::runtime_error(error.str());
    }
}
std::optional<std::uint32_t> number(io_service_t service, CFStringRef key) {
    const auto value = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    if (!value) return std::nullopt;
    std::int64_t n = 0;
    const bool valid = CFGetTypeID(value) == CFNumberGetTypeID() &&
        CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt64Type, &n) &&
        n >= INT32_MIN && n <= UINT32_MAX;
    CFRelease(value);
    return valid ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(n)) : std::nullopt;
}
struct Match { Object service; std::uint32_t pid; std::uint64_t registry_id; };
std::vector<Match> matches(std::uint32_t location) {
    io_iterator_t iterator = IO_OBJECT_NULL;
    check(IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching(kIOUSBDeviceClassName), &iterator),
          "enumerate USB identities");
    Object guard(iterator);
    std::vector<Match> result;
    while (auto next = IOIteratorNext(iterator)) {
        Object service(next);
        const auto vid = number(next, CFSTR("idVendor")), pid = number(next, CFSTR("idProduct"));
        if (vid != vendor_id || !pid || (*pid != twin_x_loader && *pid != twin_x_runtime) ||
            number(next, CFSTR("locationID")) != location) continue;
        std::uint64_t id = 0;
        check(IORegistryEntryGetRegistryEntryID(next, &id), "get registry identity");
        result.push_back({std::move(service), *pid, id});
    }
    return result;
}

class BootDevice {
    IOUSBDeviceInterface500** value = nullptr;
    bool opened = false;
    bool allow_jump = false;
    Fx3Image image;
public:
    BootDevice(io_service_t service, std::uint32_t location, Fx3Image parsed, bool execute_entrypoint)
        : allow_jump(execute_entrypoint), image(std::move(parsed)) {
        IOCFPlugInInterface** plugin = nullptr;
        SInt32 score = 0;
        check(IOCreatePlugInInterfaceForService(service, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID,
                                               &plugin, &score), "create loader user client");
        if (!plugin) throw std::runtime_error("null loader user client");
        const auto status = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID500),
                                                      reinterpret_cast<LPVOID*>(&value));
        IODestroyPlugInInterface(plugin);
        if (status != S_OK || !value) {
            if (value) (*value)->Release(value);
            value = nullptr;
            throw std::runtime_error("USB interface 500 unavailable");
        }
        try {
            // No seize, reset, SetConfiguration, interface altset or driver detach.
            check((*value)->USBDeviceOpen(value), "open loader device without seize");
            opened = true;
            UInt16 vid = 0, pid = 0;
            UInt32 actual_location = 0;
            check((*value)->GetDeviceVendor(value, &vid), "recheck loader VID");
            check((*value)->GetDeviceProduct(value, &pid), "recheck loader PID");
            check((*value)->GetLocationID(value, &actual_location), "recheck loader location");
            if (vid != vendor_id || pid != twin_x_loader || actual_location != location)
                throw std::runtime_error("loader identity changed before first write");
        } catch (...) {
            if (opened) (*value)->USBDeviceClose(value);
            (*value)->Release(value);
            value = nullptr;
            throw;
        }
    }
    ~BootDevice() {
        if (opened) (*value)->USBDeviceClose(value);
        if (value) (*value)->Release(value);
    }
    BootDevice(const BootDevice&) = delete;
    BootDevice& operator=(const BootDevice&) = delete;

    Fx3Reply control(const Fx3Request& r, const Bytes& payload) {
        if (!fx3_request_allowed(r, image, allow_jump))
            throw std::runtime_error("request outside FX3 RAM-boot policy");
        if ((r.type == 0x40 && payload.size() != r.length) || (r.type == 0xc0 && !payload.empty()))
            throw std::runtime_error("invalid RAM control payload");
        Bytes buffer = r.type == 0xc0 ? Bytes(r.length) : payload;
        IOUSBDevRequestTO request{};
        request.bmRequestType = r.type;
        request.bRequest = r.request;
        request.wValue = r.value;
        request.wIndex = r.index;
        request.wLength = r.length;
        request.pData = buffer.empty() ? nullptr : buffer.data();
        request.noDataTimeout = 1000;
        request.completionTimeout = 1500;
        check((*value)->DeviceRequestTO(value, &request), "FX3 RAM transfer (no retry)");
        return {request.wLenDone, r.type == 0xc0 ? std::move(buffer) : Bytes{}};
    }
};
}  // namespace

std::string boot_mac(const Bytes& data, std::uint32_t location, bool execute_entrypoint, bool& stage_complete) {
    // Independently retain the content gate at the hardware entry point.
    require_twin_x_image(data);
    const auto image = parse_fx3(data);
    stage_complete = false;
    bool runtime_observed = false, sequence_complete = false, loader_preserved = false;
    Fx3Progress progress;
    std::string transfer_error, observation_error;
    std::uint64_t before_id = 0;
    try {
        auto before = matches(location);
        if (before.size() != 1 || before.front().pid != twin_x_loader)
            throw std::runtime_error("need exactly one 2b5a:000e at the selected location; no device opened");
        before_id = before.front().registry_id;
        BootDevice device(before.front().service.value, location, image, execute_entrypoint);
        transfer_fx3_ram(data, [&](const auto& r, const auto& payload) { return device.control(r, payload); }, progress, execute_entrypoint);
        sequence_complete = true;
    } catch (const std::exception& error) {
        transfer_error = error.what();
    }
    if (progress.jump_attempted) {
        // Jump can disconnect before the ACK arrives. Never retry it: only an
        // actual new runtime identity at the same port can establish this step.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        try {
            do {
                const auto after = matches(location);
                if (after.size() == 1 && after.front().pid == twin_x_runtime && after.front().registry_id != before_id) {
                    runtime_observed = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } while (std::chrono::steady_clock::now() < deadline);
        } catch (const std::exception& error) {
            observation_error = error.what();
        }
    }
    if (!execute_entrypoint && sequence_complete) {
        try {
            const auto after = matches(location);
            loader_preserved = after.size() == 1 && after.front().pid == twin_x_loader && after.front().registry_id == before_id;
        } catch (const std::exception& error) {
            observation_error = error.what();
        }
    }
    stage_complete = execute_entrypoint ? runtime_observed : sequence_complete && loader_preserved;
    std::size_t expected_chunks = 0;
    for (const auto& section : image.sections)
        expected_chunks += (section.length + fx3_chunk_size - 1) / fx3_chunk_size;
    const bool ram_verified = progress.writes_completed == expected_chunks && progress.readbacks_verified == expected_chunks;
    std::ostringstream out;
    out << "{\"schema_version\":1,\"mode\":"
        << quote(execute_entrypoint ? "explicit_fx3_ram_boot" : "explicit_fx3_ram_write_verify_only")
        << ",\"entrypoint_execution_requested\":" << (execute_entrypoint ? "true" : "false")
        << ",\"image_sha256\":" << quote(sha256(data))
        << ",\"location\":" << quote(hex(location, 8))
        << ",\"progress\":{\"writes_attempted\":" << progress.writes_attempted
        << ",\"writes_completed\":" << progress.writes_completed
        << ",\"readbacks_verified\":" << progress.readbacks_verified
        << ",\"jump_attempted\":" << (progress.jump_attempted ? "true" : "false")
        << ",\"jump_acknowledged\":" << (progress.jump_acknowledged ? "true" : "false") << '}'
        << ",\"runtime_observed\":" << (runtime_observed ? "true" : "false")
        << ",\"ram_sequence_completed\":" << (ram_verified ? "true" : "false")
        << ",\"loader_preserved\":" << (loader_preserved ? "true" : "false")
        << ",\"requested_stage_complete\":" << (stage_complete ? "true" : "false")
        << ",\"transfer_error\":" << (transfer_error.empty() ? "null" : quote(transfer_error))
        << ",\"observation_error\":" << (observation_error.empty() ? "null" : quote(observation_error))
        << ",\"audio_streaming_implemented\":false,\"hardware_audio_verified\":false}";
    return out.str();
}
}  // namespace apollo

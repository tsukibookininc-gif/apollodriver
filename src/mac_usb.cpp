// SPDX-License-Identifier: GPL-2.0-only
#include "apollo/mac_usb.hpp"
#include "apollo/protocol.hpp"

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <sys/sysctl.h>

#include <array>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace apollo {
namespace {
class Object {
public:
    io_object_t value = IO_OBJECT_NULL;
    explicit Object(io_object_t v = IO_OBJECT_NULL) : value(v) {}
    ~Object() { if (value) IOObjectRelease(value); }
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&& other) noexcept : value(std::exchange(other.value, IO_OBJECT_NULL)) {}
};

void check(IOReturn result, const char* action) {
    if (result != kIOReturnSuccess)
        throw std::runtime_error(std::string(action) + ": IOKit " + hex(static_cast<std::uint32_t>(result), 8));
}

std::string cfstring(CFStringRef string) {
    if (!string) return "";
    CFIndex size = CFStringGetMaximumSizeForEncoding(CFStringGetLength(string), kCFStringEncodingUTF8) + 1;
    std::vector<char> buffer(static_cast<std::size_t>(size));
    if (!CFStringGetCString(string, buffer.data(), size, kCFStringEncodingUTF8))
        throw std::runtime_error("invalid UTF-8 property");
    return buffer.data();
}

std::optional<std::uint32_t> number(io_registry_entry_t service, CFStringRef key) {
    CFTypeRef property = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    if (!property) return std::nullopt;
    std::int64_t value = 0;
    bool valid = CFGetTypeID(property) == CFNumberGetTypeID() &&
                 CFNumberGetValue(static_cast<CFNumberRef>(property), kCFNumberSInt64Type, &value) &&
                 value >= 0 && value <= UINT32_MAX;
    CFRelease(property);
    return valid ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(value)) : std::nullopt;
}

std::string text(io_registry_entry_t service, CFStringRef key) {
    CFTypeRef property = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    if (!property) return "";
    std::string result;
    if (CFGetTypeID(property) == CFStringGetTypeID()) result = cfstring(static_cast<CFStringRef>(property));
    CFRelease(property);
    return result;
}

std::string system_string(const char* name) {
    std::size_t n = 0;
    if (sysctlbyname(name, nullptr, &n, nullptr, 0) || n == 0) return "unknown";
    std::vector<char> buffer(n + 1, 0);
    if (sysctlbyname(name, buffer.data(), &n, nullptr, 0)) return "unknown";
    return buffer.data();
}

struct Device {
    Object service;
    std::uint16_t pid;
    std::optional<std::uint32_t> location;
    std::uint64_t registry_id;
};

std::vector<Device> find_devices() {
    io_iterator_t iterator = IO_OBJECT_NULL;
    // IOUSBDevice also matches the modern IOUSBHostDevice compatibility class.
    check(IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching(kIOUSBDeviceClassName), &iterator),
          "enumerate USB services");
    Object guard(iterator);
    std::vector<Device> found;
    while (auto next = IOIteratorNext(iterator)) {
        Object service(next);
        auto vid = number(next, CFSTR("idVendor"));
        auto pid = number(next, CFSTR("idProduct"));
        if (!vid || !pid || *vid != vendor_id || (*pid != twin_x_loader && *pid != twin_x_runtime)) continue;
        std::uint64_t registry_id = 0;
        check(IORegistryEntryGetRegistryEntryID(next, &registry_id), "read registry ID");
        auto location = number(next, CFSTR("locationID"));
        found.push_back({std::move(service), static_cast<std::uint16_t>(*pid), location, registry_id});
    }
    return found;
}

template<class Interface>
Interface** query(io_service_t service, CFUUIDRef client, CFUUIDRef version) {
    IOCFPlugInInterface** plugin = nullptr;
    SInt32 score = 0;
    check(IOCreatePlugInInterfaceForService(service, client, kIOCFPlugInInterfaceID, &plugin, &score),
          "create USB user client");
    if (!plugin) throw std::runtime_error("USB user client returned null");
    Interface** result = nullptr;
    auto status = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(version), reinterpret_cast<LPVOID*>(&result));
    IODestroyPlugInInterface(plugin);
    if (status != S_OK || !result) throw std::runtime_error("requested IOUSBLib interface is unavailable");
    return result;
}

class UsbDevice {
public:
    IOUSBDeviceInterface500** value;
    explicit UsbDevice(io_service_t service)
        : value(query<IOUSBDeviceInterface500>(service, kIOUSBDeviceUserClientTypeID, kIOUSBDeviceInterfaceID500)) {}
    ~UsbDevice() { (*value)->Release(value); }
    UsbDevice(const UsbDevice&) = delete;
    UsbDevice& operator=(const UsbDevice&) = delete;
};

class VendorInterface {
public:
    IOUSBInterfaceInterface300** value;
    bool opened = false;
    explicit VendorInterface(io_service_t service)
        : value(query<IOUSBInterfaceInterface300>(service, kIOUSBInterfaceUserClientTypeID, kIOUSBInterfaceInterfaceID300)) {}
    ~VendorInterface() {
        if (opened) (*value)->USBInterfaceClose(value);
        (*value)->Release(value);
    }
    VendorInterface(const VendorInterface&) = delete;
    VendorInterface& operator=(const VendorInterface&) = delete;

    Bytes read(Read kind) {
        auto request = request_for(kind);
        if (!allowed(request)) throw std::runtime_error("request denied by read-only policy");
        Bytes result(request.length);
        IOUSBDevRequestTO native{};
        native.bmRequestType = request.type;
        native.bRequest = request.request;
        native.wValue = request.value;
        native.wIndex = request.index;
        native.wLength = request.length;
        native.pData = result.data();
        native.noDataTimeout = 1000;
        native.completionTimeout = 1500;
        check((*value)->ControlRequestTO(value, 0, &native), "read vendor status (no retry)");
        if (native.wLenDone != result.size()) throw std::runtime_error("short vendor control reply");
        return result;
    }
};

std::string configurations(UsbDevice& device) {
    UInt8 count = 0;
    check((*device.value)->GetNumberOfConfigurations(device.value, &count), "get cached configuration count");
    std::ostringstream out;
    out << '[';
    for (unsigned i = 0; i < count; ++i) {
        if (i) out << ',';
        IOUSBConfigurationDescriptorPtr pointer = nullptr;
        check((*device.value)->GetConfigurationDescriptorPtr(device.value, static_cast<UInt8>(i), &pointer),
              "get cached configuration descriptor");
        if (!pointer) throw std::runtime_error("null cached descriptor");
        auto raw = reinterpret_cast<const std::uint8_t*>(pointer);
        std::size_t size = std::size_t(raw[2]) | (std::size_t(raw[3]) << 8);
        if (size < 9) throw std::runtime_error("short cached configuration");
        out << decode_configuration(Bytes(raw, raw + size));
    }
    out << ']';
    return out.str();
}

std::string vendor_status(UsbDevice& device) {
    IOUSBFindInterfaceRequest filter{kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare,
                                     kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare};
    io_iterator_t iterator = IO_OBJECT_NULL;
    check((*device.value)->CreateInterfaceIterator(device.value, &filter, &iterator), "enumerate active USB interfaces");
    Object guard(iterator);
    while (auto next = IOIteratorNext(iterator)) {
        Object service(next);
        VendorInterface iface(next);
        UInt8 number_value = 255, class_value = 0;
        check((*iface.value)->GetInterfaceNumber(iface.value, &number_value), "read interface number");
        check((*iface.value)->GetInterfaceClass(iface.value, &class_value), "read interface class");
        if (number_value != 0 || class_value != 0xff) continue;
        // Never seize an interface from an existing driver and never change its altset.
        check((*iface.value)->USBInterfaceOpen(iface.value), "open vendor interface 0 without seize");
        iface.opened = true;
        std::ostringstream out;
        out << '{';
        bool first = true;
        for (auto kind : {Read::protocol, Read::status, Read::version_a, Read::version_b, Read::dsp0_boot}) {
            if (!first) out << ',';
            first = false;
            out << quote(read_name(kind)) << ':';
            try {
                auto raw = iface.read(kind);
                auto decoded = decode_read(kind, raw);
                out << "{\"raw_hex\":" << quote(hex_bytes(raw)) << ",\"decoded\":" << decoded << '}';
            } catch (const std::exception& error) {
                out << "{\"error\":" << quote(error.what()) << '}';
                // A stall or invalid reply does not justify more requests to an unknown state.
                out << ",\"stopped_after_error\":true";
                break;
            }
        }
        out << '}';
        return out.str();
    }
    throw std::runtime_error("no vendor-class interface 0; device configuration was not changed");
}

std::string audio_text(AudioObjectID id, AudioObjectPropertySelector selector) {
    AudioObjectPropertyAddress address{selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    CFStringRef value = nullptr;
    UInt32 size = sizeof(value);
    if (AudioObjectGetPropertyData(id, &address, 0, nullptr, &size, &value) != noErr) return "";
    std::string result = cfstring(value);
    if (value) CFRelease(value);
    return result;
}

std::string audio_channels(AudioDeviceID id, AudioObjectPropertyScope scope) {
    AudioObjectPropertyAddress address{kAudioDevicePropertyStreamConfiguration, scope, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(id, &address, 0, nullptr, &size) != noErr || size < sizeof(UInt32)) return "null";
    // max_align_t storage keeps AudioBufferList aligned on both architectures.
    std::vector<std::max_align_t> storage((size + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
    if (AudioObjectGetPropertyData(id, &address, 0, nullptr, &size, storage.data()) != noErr) return "null";
    auto* list = reinterpret_cast<const AudioBufferList*>(storage.data());
    std::size_t header = offsetof(AudioBufferList, mBuffers);
    if (size < header || list->mNumberBuffers > (size - header) / sizeof(AudioBuffer)) return "null";
    std::uint64_t channels = 0;
    for (UInt32 i = 0; i < list->mNumberBuffers; ++i) channels += list->mBuffers[i].mNumberChannels;
    return std::to_string(channels);
}

std::string audio_inventory() {
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    auto status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size);
    if (status != noErr) throw std::runtime_error("Core Audio inventory unavailable: " + std::to_string(status));
    std::vector<AudioDeviceID> ids((size + sizeof(AudioDeviceID) - 1) / sizeof(AudioDeviceID));
    if (size) {
        status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, ids.data());
        if (status != noErr) throw std::runtime_error("Core Audio inventory changed: " + std::to_string(status));
    }
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < size / sizeof(AudioDeviceID); ++i) {
        if (i) out << ',';
        out << "{\"id\":" << ids[i] << ",\"name\":" << quote(audio_text(ids[i], kAudioObjectPropertyName))
            << ",\"manufacturer\":" << quote(audio_text(ids[i], kAudioObjectPropertyManufacturer))
            << ",\"input_channels\":" << audio_channels(ids[i], kAudioObjectPropertyScopeInput)
            << ",\"output_channels\":" << audio_channels(ids[i], kAudioObjectPropertyScopeOutput) << '}';
    }
    out << ']';
    return out.str();
}
}  // namespace

std::string inspect_mac(const ScanOptions& options) {
    auto devices = find_devices();
    std::size_t selected = 0;
    for (const auto& device : devices)
        if (!options.location || device.location == options.location) ++selected;
    if (options.status && selected != 1)
        throw std::runtime_error("--status requires exactly one Twin X USB; use --location HEX when multiple are attached");
    std::ostringstream out;
    int arm64 = 0;
    std::size_t arm64_size = sizeof(arm64);
    bool host_known = sysctlbyname("hw.optional.arm64", &arm64, &arm64_size, nullptr, 0) == 0;
#if defined(__arm64__) || defined(__aarch64__)
    const char* process_arch = "arm64";
#else
    const char* process_arch = "x86_64";
#endif
    out << "{\"schema_version\":1,\"macos\":" << quote(system_string("kern.osproductversion"))
        << ",\"machine\":" << quote(system_string("hw.machine"))
        << ",\"process_arch\":" << quote(process_arch)
        << ",\"host_arm64\":" << (host_known ? (arm64 ? "true" : "false") : "null")
        << ",\"audio_streaming_implemented\":false,\"hardware_audio_verified\":false"
        << ",\"mode\":" << quote(options.status ? "known_vendor_reads" : (options.descriptors ? "cached_descriptors" : "registry_only"))
        << ",\"devices\":[";
    bool first = true;
    for (const auto& device : devices) {
        if (options.location && device.location != options.location) continue;
        if (!first) out << ',';
        first = false;
        out << "{\"vid\":\"0x2b5a\",\"pid\":" << quote(hex(device.pid, 4))
            << ",\"state\":" << quote(device.pid == twin_x_loader ? "fx3_loader" : "runtime_usb_identity")
            << ",\"product\":" << quote(text(device.service.value, CFSTR("USB Product Name")))
            << ",\"registry_id\":" << quote(hex(device.registry_id))
            << ",\"location\":" << (device.location ? quote(hex(*device.location, 8)) : "null");
        if (options.descriptors || options.status) {
            try {
                UsbDevice usb(device.service.value);
                if (options.descriptors) {
                    // Build first so an exception cannot leave a partially written JSON field.
                    auto config = configurations(usb);
                    out << ",\"configurations\":" << config;
                    UInt8 speed = 0;
                    if ((*usb.value)->GetDeviceSpeed(usb.value, &speed) == kIOReturnSuccess)
                        out << ",\"iokit_speed_code\":" << unsigned(speed);
                }
                if (options.status) {
                    if (device.pid == twin_x_loader)
                        out << ",\"status_error\":\"FX3 loader: no vendor runtime commands were sent; firmware not uploaded\"";
                    else {
                        auto status = vendor_status(usb);
                        out << ",\"vendor_reads\":" << status;
                    }
                }
            } catch (const std::exception& error) {
                out << ",\"usb_error\":" << quote(error.what());
            }
        }
        out << '}';
    }
    out << ']';
    try {
        auto audio = audio_inventory();
        out << ",\"core_audio_devices\":" << audio;
    } catch (const std::exception& error) {
        out << ",\"core_audio_error\":" << quote(error.what());
    }
    out << '}';
    return out.str();
}
}  // namespace apollo

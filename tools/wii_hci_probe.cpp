#include <libusb.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr uint16_t kDefaultVid = 0x0A12;
constexpr uint16_t kDefaultPid = 0x0001;
constexpr uint8_t kHciClass = 0xE0;
constexpr uint8_t kHciSubclass = 0x01;
constexpr uint8_t kHciProtocol = 0x01;
constexpr uint8_t kHciEventCommandComplete = 0x0E;
constexpr uint8_t kHciEventCommandStatus = 0x0F;
constexpr unsigned kEventTimeoutMs = 3000;

struct HciInterface {
    int number = -1;
    int alternate = 0;
    uint8_t eventIn = 0;
};

bool ParseId(const char* text, uint16_t& value) {
    if (!text || !*text) return false;
    errno = 0;
    char* end = nullptr;
    const auto parsed = std::strtoul(text, &end, 0);
    if (errno || end == text || *end || parsed > 0xFFFF) return false;
    value = static_cast<uint16_t>(parsed);
    return true;
}

bool FindHciInterface(libusb_device* device, HciInterface& out) {
    libusb_config_descriptor* config = nullptr;
    const int rc = libusb_get_active_config_descriptor(device, &config);
    if (rc != LIBUSB_SUCCESS || !config) {
        std::fprintf(stderr, "  active configuration unavailable: %s\n", libusb_error_name(rc));
        return false;
    }

    bool found = false;
    for (uint8_t i = 0; i < config->bNumInterfaces && !found; ++i) {
        const auto& group = config->interface[i];
        for (int a = 0; a < group.num_altsetting && !found; ++a) {
            const auto& alt = group.altsetting[a];
            if (alt.bInterfaceClass != kHciClass ||
                alt.bInterfaceSubClass != kHciSubclass ||
                alt.bInterfaceProtocol != kHciProtocol) continue;

            for (uint8_t e = 0; e < alt.bNumEndpoints; ++e) {
                const auto& ep = alt.endpoint[e];
                const auto type = ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
                const bool input = (ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN;
                if (type == LIBUSB_TRANSFER_TYPE_INTERRUPT && input) {
                    out.number = alt.bInterfaceNumber;
                    out.alternate = alt.bAlternateSetting;
                    out.eventIn = ep.bEndpointAddress;
                    found = true;
                    break;
                }
            }
        }
    }
    libusb_free_config_descriptor(config);
    return found;
}

bool ReadCommandEvent(libusb_device_handle* handle, uint8_t endpoint,
                      uint16_t expectedOpcode, std::vector<uint8_t>& event) {
    std::array<uint8_t, 64> buffer{};
    for (int attempt = 0; attempt < 16; ++attempt) {
        int transferred = 0;
        const int rc = libusb_interrupt_transfer(handle, endpoint, buffer.data(),
                                                  static_cast<int>(buffer.size()),
                                                  &transferred, kEventTimeoutMs);
        if (rc != LIBUSB_SUCCESS) {
            std::fprintf(stderr, "  HCI event read failed: %s\n", libusb_error_name(rc));
            return false;
        }
        if (transferred < 2) {
            std::fprintf(stderr, "  ignoring truncated HCI event (%d bytes)\n", transferred);
            continue;
        }

        const uint8_t eventCode = buffer[0];
        const uint8_t parameterLength = buffer[1];
        if (static_cast<int>(parameterLength) + 2 > transferred) {
            std::fprintf(stderr, "  ignoring malformed HCI event (declared %u, received %d)\n",
                         parameterLength, transferred);
            continue;
        }

        if (eventCode == kHciEventCommandComplete && parameterLength >= 3) {
            const uint16_t opcode = static_cast<uint16_t>(buffer[3]) |
                                    (static_cast<uint16_t>(buffer[4]) << 8);
            if (opcode == expectedOpcode) {
                event.assign(buffer.begin(), buffer.begin() + transferred);
                return true;
            }
        } else if (eventCode == kHciEventCommandStatus && parameterLength >= 4) {
            const uint16_t opcode = static_cast<uint16_t>(buffer[4]) |
                                    (static_cast<uint16_t>(buffer[5]) << 8);
            if (opcode == expectedOpcode) {
                event.assign(buffer.begin(), buffer.begin() + transferred);
                return true;
            }
        }

        std::printf("  unrelated HCI event 0x%02x (%u parameter bytes)\n",
                    eventCode, parameterLength);
    }
    std::fprintf(stderr, "  timed out waiting for HCI opcode 0x%04x\n", expectedOpcode);
    return false;
}

bool SendCommand(libusb_device_handle* handle, uint8_t eventEndpoint,
                 uint16_t opcode, const uint8_t* parameters, uint8_t parameterLength,
                 std::vector<uint8_t>& event) {
    std::array<uint8_t, 258> packet{};
    packet[0] = static_cast<uint8_t>(opcode & 0xFF);
    packet[1] = static_cast<uint8_t>(opcode >> 8);
    packet[2] = parameterLength;
    if (parameterLength && parameters)
        std::memcpy(packet.data() + 3, parameters, parameterLength);

    // USB Bluetooth HCI commands use the default control pipe (not bulk OUT).
    const uint8_t requestType = LIBUSB_ENDPOINT_OUT |
                                LIBUSB_REQUEST_TYPE_CLASS |
                                LIBUSB_RECIPIENT_DEVICE;
    const int rc = libusb_control_transfer(handle, requestType, 0x00, 0x0000, 0x0000,
                                           packet.data(), static_cast<uint16_t>(parameterLength + 3),
                                           kEventTimeoutMs);
    if (rc < 0) {
        std::fprintf(stderr, "  HCI command 0x%04x control transfer failed: %s\n",
                     opcode, libusb_error_name(rc));
        return false;
    }
    if (rc != parameterLength + 3) {
        std::fprintf(stderr, "  HCI command 0x%04x short write: %d/%u bytes\n",
                     opcode, rc, static_cast<unsigned>(parameterLength + 3));
        return false;
    }
    std::printf("  TX HCI command 0x%04x (%u parameter bytes)\n", opcode, parameterLength);

    if (!ReadCommandEvent(handle, eventEndpoint, opcode, event)) return false;
    if (event[0] == kHciEventCommandComplete) {
        if (event.size() < 6) {
            std::fprintf(stderr, "  malformed Command Complete for opcode 0x%04x\n", opcode);
            return false;
        }
        const uint8_t status = event[5];
        std::printf("  RX Command Complete opcode 0x%04x status 0x%02x\n", opcode, status);
        if (status != 0) {
            std::fprintf(stderr, "  controller rejected opcode 0x%04x (status 0x%02x)\n",
                         opcode, status);
            return false;
        }
        return true;
    }

    const uint8_t status = event.size() > 2 ? event[2] : 0xFF;
    std::printf("  RX Command Status opcode 0x%04x status 0x%02x\n", opcode, status);
    return status == 0;
}

bool RunHciProbe(libusb_device* device, uint16_t vid, uint16_t pid) {
    libusb_device_descriptor descriptor{};
    int rc = libusb_get_device_descriptor(device, &descriptor);
    if (rc != LIBUSB_SUCCESS || descriptor.idVendor != vid || descriptor.idProduct != pid)
        return false;

    std::printf("\nAdapter %04x:%04x bus %u address %u\n", vid, pid,
                libusb_get_bus_number(device), libusb_get_device_address(device));

    HciInterface hci;
    if (!FindHciInterface(device, hci)) {
        std::puts("  no active standard HCI interface with interrupt event endpoint");
        return false;
    }
    std::printf("  HCI interface %d alt %d; event endpoint 0x%02x\n",
                hci.number, hci.alternate, hci.eventIn);

    libusb_device_handle* handle = nullptr;
    rc = libusb_open(device, &handle);
    if (rc != LIBUSB_SUCCESS) {
        std::fprintf(stderr, "  USB open failed: %s\n", libusb_error_name(rc));
        return false;
    }

    bool claimed = false;
    bool success = false;
    rc = libusb_claim_interface(handle, hci.number);
    if (rc != LIBUSB_SUCCESS) {
        std::fprintf(stderr, "  claim interface failed: %s\n", libusb_error_name(rc));
        libusb_close(handle);
        return false;
    }
    claimed = true;

    if (hci.alternate != 0) {
        rc = libusb_set_interface_alt_setting(handle, hci.number, hci.alternate);
        if (rc != LIBUSB_SUCCESS) {
            std::fprintf(stderr, "  set alternate setting failed: %s\n", libusb_error_name(rc));
            goto cleanup;
        }
    }

    {
        std::vector<uint8_t> event;
        if (!SendCommand(handle, hci.eventIn, 0x0C03, nullptr, 0, event)) goto cleanup;
        std::puts("  HCI Reset: OK");

        if (!SendCommand(handle, hci.eventIn, 0x1001, nullptr, 0, event)) goto cleanup;
        if (event[0] != kHciEventCommandComplete || event.size() < 14) {
            std::fprintf(stderr, "  Read Local Version Information returned an unexpected event\n");
            goto cleanup;
        }
        const uint8_t* p = event.data() + 6;
        const uint8_t hciVersion = p[0];
        const uint16_t hciRevision = static_cast<uint16_t>(p[1]) |
                                     (static_cast<uint16_t>(p[2]) << 8);
        const uint8_t lmpVersion = p[3];
        const uint16_t manufacturer = static_cast<uint16_t>(p[4]) |
                                      (static_cast<uint16_t>(p[5]) << 8);
        const uint16_t lmpSubversion = static_cast<uint16_t>(p[6]) |
                                       (static_cast<uint16_t>(p[7]) << 8);
        std::printf("  HCI version: 0x%02x; revision: 0x%04x\n", hciVersion, hciRevision);
        std::printf("  LMP version: 0x%02x; manufacturer: 0x%04x; subversion: 0x%04x\n",
                    lmpVersion, manufacturer, lmpSubversion);

        if (!SendCommand(handle, hci.eventIn, 0x1009, nullptr, 0, event)) goto cleanup;
        if (event[0] != kHciEventCommandComplete || event.size() < 12) {
            std::fprintf(stderr, "  Read BD_ADDR returned an unexpected event\n");
            goto cleanup;
        }
        const uint8_t* address = event.data() + 6;
        std::printf("  BD_ADDR: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    address[5], address[4], address[3],
                    address[2], address[1], address[0]);
    }
    success = true;

cleanup:
    if (claimed) {
        const int releaseRc = libusb_release_interface(handle, hci.number);
        if (releaseRc == LIBUSB_SUCCESS) std::puts("  interface released cleanly");
        else std::fprintf(stderr, "  interface release warning: %s\n", libusb_error_name(releaseRc));
    }
    libusb_close(handle);
    if (success) std::puts("PASS: HCI command/event exchange completed.");
    return success;
}
} // namespace

int main(int argc, char** argv) {
    uint16_t vid = kDefaultVid;
    uint16_t pid = kDefaultPid;
    if (argc == 2 && std::strcmp(argv[1], "--help") == 0) {
        std::puts("Usage: wii_hci_probe [VID PID]\nDefault: 0x0A12 0x0001");
        return 0;
    }
    if (argc != 1 && (argc != 3 || !ParseId(argv[1], vid) || !ParseId(argv[2], pid))) {
        std::fprintf(stderr, "Usage: wii_hci_probe [VID PID]\n");
        return 2;
    }

    libusb_context* context = nullptr;
    int rc = libusb_init(&context);
    if (rc != LIBUSB_SUCCESS) {
        std::fprintf(stderr, "libusb_init failed: %s\n", libusb_error_name(rc));
        return 1;
    }
    libusb_set_option(context, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);

    libusb_device** devices = nullptr;
    const ssize_t count = libusb_get_device_list(context, &devices);
    if (count < 0) {
        std::fprintf(stderr, "USB enumeration failed: %s\n",
                     libusb_error_name(static_cast<int>(count)));
        libusb_exit(context);
        return 1;
    }

    std::printf("HOTD2RE USB Bluetooth HCI probe\nTarget %04x:%04x; %lld USB devices enumerated\n",
                vid, pid, static_cast<long long>(count));
    bool found = false;
    bool passed = false;
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device_descriptor descriptor{};
        if (libusb_get_device_descriptor(devices[i], &descriptor) != LIBUSB_SUCCESS ||
            descriptor.idVendor != vid || descriptor.idProduct != pid) continue;
        found = true;
        if (RunHciProbe(devices[i], vid, pid)) {
            passed = true;
            break;
        }
    }

    libusb_free_device_list(devices, 1);
    libusb_exit(context);
    if (!found) {
        std::puts("Adapter not found; check USB connection and VID/PID.");
        return 3;
    }
    if (!passed) {
        std::puts("FAIL: HCI command/event exchange did not complete.");
        return 5;
    }
    return 0;
}

#include <libusb.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
const char* TransferName(uint8_t t) {
    switch(t) {
    case LIBUSB_TRANSFER_TYPE_CONTROL:return "control";
    case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:return "isochronous";
    case LIBUSB_TRANSFER_TYPE_BULK:return "bulk";
    case LIBUSB_TRANSFER_TYPE_INTERRUPT:return "interrupt";
    default:return "unknown";
    }
}
bool ParseId(const char* s,uint16_t& out) {
    if(!s||!*s)return false; errno=0; char* end=nullptr;
    const auto n=std::strtoul(s,&end,0);
    if(errno||end==s||*end||n>0xffff)return false;
    out=static_cast<uint16_t>(n);return true;
}
std::string UsbString(libusb_device_handle* h,uint8_t index) {
    if(!h||!index)return {};
    unsigned char b[256]{};
    int n=libusb_get_string_descriptor_ascii(h,index,b,sizeof(b));
    return n>0?std::string(reinterpret_cast<char*>(b),static_cast<size_t>(n)):std::string{};
}
bool Inspect(const libusb_interface_descriptor& a) {
    bool irqIn=false,bulkIn=false,bulkOut=false;
    std::printf("    interface %u alt %u: class %02x subclass %02x protocol %02x\n",
        a.bInterfaceNumber,a.bAlternateSetting,a.bInterfaceClass,a.bInterfaceSubClass,a.bInterfaceProtocol);
    for(uint8_t i=0;i<a.bNumEndpoints;++i) {
        const auto& ep=a.endpoint[i];
        const auto type=static_cast<uint8_t>(ep.bmAttributes&LIBUSB_TRANSFER_TYPE_MASK);
        const bool in=(ep.bEndpointAddress&LIBUSB_ENDPOINT_DIR_MASK)==LIBUSB_ENDPOINT_IN;
        std::printf("      endpoint 0x%02x %-11s %s max-packet %u\n",ep.bEndpointAddress,
            TransferName(type),in?"IN ":"OUT",ep.wMaxPacketSize);
        irqIn|=type==LIBUSB_TRANSFER_TYPE_INTERRUPT&&in;
        bulkIn|=type==LIBUSB_TRANSFER_TYPE_BULK&&in;
        bulkOut|=type==LIBUSB_TRANSFER_TYPE_BULK&&!in;
    }
    const bool bluetoothClass=a.bInterfaceClass==0xE0&&a.bInterfaceSubClass==0x01&&a.bInterfaceProtocol==0x01;
    const bool layout=irqIn&&bulkIn&&bulkOut;
    if(bluetoothClass&&layout)std::puts("      candidate: standard USB Bluetooth HCI interface");
    return bluetoothClass&&layout;
}
bool HasStandardHciLayout(libusb_device* device) {
    libusb_config_descriptor* config = nullptr;
    if (libusb_get_active_config_descriptor(device, &config) != LIBUSB_SUCCESS || !config)
        return false;
    bool found = false;
    for (uint8_t i = 0; i < config->bNumInterfaces && !found; ++i) {
        const auto& group = config->interface[i];
        for (int a = 0; a < group.num_altsetting && !found; ++a) {
            const auto& alt = group.altsetting[a];
            if (alt.bInterfaceClass != 0xE0 ||
                alt.bInterfaceSubClass != 0x01 ||
                alt.bInterfaceProtocol != 0x01)
                continue;
            bool interruptIn = false, bulkIn = false, bulkOut = false;
            for (uint8_t e = 0; e < alt.bNumEndpoints; ++e) {
                const auto& ep = alt.endpoint[e];
                const auto type = ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
                const bool in = (ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN;
                interruptIn |= type == LIBUSB_TRANSFER_TYPE_INTERRUPT && in;
                bulkIn |= type == LIBUSB_TRANSFER_TYPE_BULK && in;
                bulkOut |= type == LIBUSB_TRANSFER_TYPE_BULK && !in;
            }
            found = interruptIn && bulkIn && bulkOut;
        }
    }
    libusb_free_config_descriptor(config);
    return found;
}
bool Probe(libusb_device* d,bool filter,uint16_t vid,uint16_t pid) {
    libusb_device_descriptor desc{};
    int rc=libusb_get_device_descriptor(d,&desc);
    if(rc!=LIBUSB_SUCCESS||(filter&&(desc.idVendor!=vid||desc.idProduct!=pid)))return false;
    std::printf("\nFound adapter %04x:%04x at bus %u address %u\n",desc.idVendor,desc.idProduct,
        libusb_get_bus_number(d),libusb_get_device_address(d));
    libusb_device_handle* h=nullptr;
    rc=libusb_open(d,&h);
    if(rc!=LIBUSB_SUCCESS) {
        std::printf("  OPEN FAILED: %s (check the WinUSB/libusbK driver)\n",libusb_error_name(rc));return false;
    }
    auto manufacturer=UsbString(h,desc.iManufacturer),product=UsbString(h,desc.iProduct),serial=UsbString(h,desc.iSerialNumber);
    std::printf("  manufacturer: %s\n  product: %s\n  serial: %s\n",
        manufacturer.empty()?"(unavailable)":manufacturer.c_str(),
        product.empty()?"(unavailable)":product.c_str(),serial.empty()?"(not exposed)":serial.c_str());
    bool found=false,claimed=false;int claimedInterface=-1;
    for(uint8_t ci=0;ci<desc.bNumConfigurations;++ci) {
        libusb_config_descriptor* cfg=nullptr;
        rc=libusb_get_config_descriptor(d,ci,&cfg);
        if(rc!=LIBUSB_SUCCESS||!cfg){std::printf("  config read failed: %s\n",libusb_error_name(rc));continue;}
        std::printf("  configuration %u value %u: %u interface groups\n",ci,cfg->bConfigurationValue,cfg->bNumInterfaces);
        for(int i=0;i<cfg->bNumInterfaces;++i) {
            const auto& group=cfg->interface[i];
            for(int a=0;a<group.num_altsetting;++a) {
                const auto& alt=group.altsetting[a];
                if(!Inspect(alt))continue;
                found=true;
                if(claimed)continue;
                rc=libusb_claim_interface(h,alt.bInterfaceNumber);
                if(rc==LIBUSB_SUCCESS) {
                    claimed=true;claimedInterface=alt.bInterfaceNumber;
                    std::printf("      CLAIM OK: interface %d accessible through libusb\n",claimedInterface);
                } else std::printf("      CLAIM FAILED: %s\n",libusb_error_name(rc));
            }
        }
        libusb_free_config_descriptor(cfg);
    }
    if(claimed){libusb_release_interface(h,claimedInterface);std::puts("  interface released cleanly");}
    libusb_close(h);
    if(!found)std::puts("  no standard Bluetooth HCI interface layout found");
    if(found&&!claimed)std::puts("  HCI layout found, but interface claim failed");
    return found&&claimed;
}
}
int main(int argc,char** argv) {
    uint16_t vid=0,pid=0;bool filter=false;
    if(argc==2&&std::strcmp(argv[1],"--help")==0) {
        std::puts("Usage: wii_bluetooth_probe [VID PID]\nDefault: enumerate all USB devices and report claimable Bluetooth HCI adapters.");return 0;
    }
    if(argc!=1) {
        if(argc!=3||!ParseId(argv[1],vid)||!ParseId(argv[2],pid)) {
            std::fprintf(stderr,"Usage: wii_bluetooth_probe [VID PID]\n");return 2;
        }
        filter=true;
    }
    libusb_context* ctx=nullptr;
    int rc=libusb_init(&ctx);
    if(rc!=LIBUSB_SUCCESS){std::fprintf(stderr,"libusb_init: %s\n",libusb_error_name(rc));return 1;}
    libusb_set_option(ctx,LIBUSB_OPTION_LOG_LEVEL,LIBUSB_LOG_LEVEL_WARNING);
    libusb_device** list=nullptr;
    const ssize_t count=libusb_get_device_list(ctx,&list);
    if(count<0){std::fprintf(stderr,"USB enumeration: %s\n",libusb_error_name(static_cast<int>(count)));libusb_exit(ctx);return 1;}
    if(filter)std::printf("HOTD2RE USB Bluetooth probe\nFilter %04x:%04x; %lld USB devices enumerated\n",vid,pid,static_cast<long long>(count));
    else std::printf("HOTD2RE USB Bluetooth probe\nEnumerating all USB devices; %lld total\n",static_cast<long long>(count));
    bool found=false,usable=false;
    for(ssize_t i=0;i<count;++i) {
        libusb_device_descriptor d{};
        if(libusb_get_device_descriptor(list[i],&d)!=LIBUSB_SUCCESS)continue;
        if(filter&&(d.idVendor!=vid||d.idProduct!=pid))continue;
        if(!filter&&!HasStandardHciLayout(list[i]))continue;
        found=true;usable=Probe(list[i],filter,vid,pid)||usable;
    }
    libusb_free_device_list(list,1);libusb_exit(ctx);
    if(!found){std::puts(filter?"Adapter not found; check USB connection and VID/PID.":"No USB devices could be inspected.");return 3;}
    if(!usable){std::puts("No claimable standard USB Bluetooth HCI adapter was detected.");return 4;}
    std::puts("PASS: adapter exposes a claimable USB Bluetooth HCI interface.");return 0;
}
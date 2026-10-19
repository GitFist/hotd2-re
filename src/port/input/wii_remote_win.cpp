#include "wii_remote.h"
#ifdef _WIN32
#include <windows.h>
#include <bluetoothapis.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace input::wii {
namespace {
using Clock=std::chrono::steady_clock;
class Handle {
public:
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h=INVALID_HANDLE_VALUE):value(h){}
    ~Handle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    explicit operator bool()const{return value && value!=INVALID_HANDLE_VALUE;}
};
bool Write(HANDLE h,const uint8_t* data,size_t size,size_t outputLength) {
    if(size>64 || outputLength>64)return false;
    uint8_t report[64]{};std::copy(data,data+size,report);
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!event)return false;
    OVERLAPPED ov{};ov.hEvent=event.value;DWORD written=0;
    BOOL ok=WriteFile(h,report,DWORD(std::max(size,outputLength)),&written,&ov);
    if(!ok && GetLastError()==ERROR_IO_PENDING) {
        if(WaitForSingleObject(event.value,250)!=WAIT_OBJECT_0){CancelIoEx(h,&ov);GetOverlappedResult(h,&ov,&written,TRUE);return false;}
        ok=GetOverlappedResult(h,&ov,&written,FALSE);
    }
    return ok && written>0;
}
// Each remote owns its HID I/O thread. Bluetooth discovery never interrupts reports
// from a controller already in use. No driver replacement or global device removal.
class Device {
public:
    std::wstring path;
    std::atomic<bool> stop{false},alive{false};
    std::atomic<int> player{0};
    explicit Device(std::wstring p,int slot):path(std::move(p)),player(slot){}
    ~Device(){stop=true;if(thread.joinable())thread.join();}
    bool Open() {
        handle.value=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
        if(!handle)return false;
        HIDD_ATTRIBUTES attr{};attr.Size=sizeof(attr);
        if(!HidD_GetAttributes(handle.value,&attr) || attr.VendorID!=0x057e || (attr.ProductID!=0x0306 && attr.ProductID!=0x0330))return false;
        PHIDP_PREPARSED_DATA prepared=nullptr;HIDP_CAPS caps{};
        if(!HidD_GetPreparsedData(handle.value,&prepared))return false;
        NTSTATUS status=HidP_GetCaps(prepared,&caps);HidD_FreePreparsedData(prepared);
        if(status!=HIDP_STATUS_SUCCESS || caps.InputReportByteLength<18 || caps.InputReportByteLength>64 || caps.OutputReportByteLength>64)return false;
        inputLength=caps.InputReportByteLength;outputLength=std::max<size_t>(22,caps.OutputReportByteLength);
        HidD_SetNumInputBuffers(handle.value,64);
        // DolphinBar exposes empty slots too. Require an actual response before assigning a player.
        uint8_t request[]{0x15,0};if(!Write(handle.value,request,2,outputLength))return false;
        uint8_t report[64]{};DWORD bytes=0;
        Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!event)return false;
        OVERLAPPED ov{};ov.hEvent=event.value;
        BOOL ok=ReadFile(handle.value,report,DWORD(inputLength),&bytes,&ov);
        if(!ok && GetLastError()==ERROR_IO_PENDING){
            if(WaitForSingleObject(event.value,700)!=WAIT_OBJECT_0){CancelIoEx(handle.value,&ov);GetOverlappedResult(handle.value,&ov,&bytes,TRUE);return false;}
            ok=GetOverlappedResult(handle.value,&ov,&bytes,FALSE);
        }
        if(!ok || !DecodeReport(report,bytes,state))return false;
        state.connected=state.reserved=true;state.pressed=0;
        alive=true;thread=std::thread([this]{Run();});return true;
    }
    Remote Poll(){std::lock_guard lock(mutex);Remote r=state;r.connected=alive;r.reserved=true;state.pressed=0;return r;}
private:
    Handle handle;
    std::thread thread;
    std::mutex mutex;
    Remote state;
    size_t inputLength=22,outputLength=22;
    bool Send(std::initializer_list<uint8_t> bytes){return Write(handle.value,bytes.begin(),bytes.size(),outputLength);}
    bool InitIR(){
        const uint8_t start=8,mode=3;
        const uint8_t sensitivity1[]{2,0,0,0x71,1,0,0xaa,0,0x64},sensitivity2[]{0x63,3};
        if(!Send({0x13,4}) || !Send({0x1a,4}))return false;
        for(auto cmd:{WriteRegister(0xb00030,&start,1),WriteRegister(0xb00000,sensitivity1,9),
                     WriteRegister(0xb0001a,sensitivity2,2),WriteRegister(0xb00033,&mode,1),WriteRegister(0xb00030,&start,1)}){
            if(stop || !Write(handle.value,cmd.data(),cmd.size(),outputLength))return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
        }
        return Send({0x12,4,0x33}); // continuous buttons, accelerometer and 12-byte extended IR
    }
    void Run(){
        if(!InitIR()){alive=false;return;}
        Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!event){alive=false;return;}
        auto last=Clock::now(),statusAt=last;int led=-1;
        uint8_t report[64]{};DWORD bytes=0;OVERLAPPED ov{};ov.hEvent=event.value;bool pending=false;
        while(!stop){
            if(led!=player){led=player;if(!Send({0x11,uint8_t(0x10<<led)}))break;}
            if(!pending){
                ResetEvent(event.value);bytes=0;
                BOOL ok=ReadFile(handle.value,report,DWORD(inputLength),&bytes,&ov);
                if(!ok && GetLastError()!=ERROR_IO_PENDING)break;
                pending=!ok;
            }
            if(pending){
                DWORD result=WaitForSingleObject(event.value,10);
                if(result==WAIT_OBJECT_0){if(!GetOverlappedResult(handle.value,&ov,&bytes,FALSE))break;pending=false;}
                else if(result!=WAIT_TIMEOUT)break;
            }
            if(!pending && bytes){
                bool valid;
                {std::lock_guard lock(mutex);valid=DecodeReport(report,bytes,state);}
                if(valid)last=Clock::now();
                // Plugging an extension changes the reporting mode; restore our core/IR format.
                if(report[0]==0x20 && !Send({0x12,4,0x33}))break;
                bytes=0;
            }
            auto now=Clock::now();
            if(now-last>std::chrono::milliseconds(300)){std::lock_guard lock(mutex);state.tracking=false;state.buttons=state.pressed=0;}
            if(now-last>std::chrono::seconds(5))break;
            if(now-statusAt>std::chrono::seconds(10)){if(!Send({0x15,0}))break;statusAt=now;}
        }
        if(pending){CancelIoEx(handle.value,&ov);GetOverlappedResult(handle.value,&ov,&bytes,TRUE);}
        Send({0x11,0}); // release our player light and stop rumble; leave Windows pairing intact
        alive=false;
    }
};
std::vector<std::wstring> HidPaths(){
    std::vector<std::wstring> paths;GUID guid;HidD_GetHidGuid(&guid);
    HDEVINFO list=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(list==INVALID_HANDLE_VALUE)return paths;
    SP_DEVICE_INTERFACE_DATA item{};item.cbSize=sizeof(item);
    for(DWORD i=0;SetupDiEnumDeviceInterfaces(list,nullptr,&guid,i,&item);++i){
        DWORD size=0;SetupDiGetDeviceInterfaceDetailW(list,&item,nullptr,0,&size,nullptr);
        if(size<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || size>65536)continue;
        std::vector<uint8_t> storage(size);auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());detail->cbSize=sizeof(*detail);
        if(!SetupDiGetDeviceInterfaceDetailW(list,&item,detail,size,nullptr,nullptr))continue;
        std::wstring path=detail->DevicePath,lower=path;std::transform(lower.begin(),lower.end(),lower.begin(),towlower);
        // Inspect only Nintendo Wii Remote interfaces, including DolphinBar Mode 4.
        if(lower.find(L"vid&0002057e")!=std::wstring::npos || lower.find(L"vid_057e")!=std::wstring::npos)paths.push_back(path);
    }
    SetupDiDestroyDeviceInfoList(list);return paths;
}
bool DiscoverBluetooth(const std::atomic<bool>& stop){
    bool radioFound=false;BLUETOOTH_FIND_RADIO_PARAMS rp{sizeof(rp)};HANDLE raw=nullptr;
    HBLUETOOTH_RADIO_FIND radios=BluetoothFindFirstRadio(&rp,&raw);if(!radios)return false;
    do{
        Handle radio(raw);radioFound=true;if(stop)break;
        BLUETOOTH_RADIO_INFO ri{};ri.dwSize=sizeof(ri);BluetoothGetRadioInfo(radio.value,&ri);
        BLUETOOTH_DEVICE_SEARCH_PARAMS search{};search.dwSize=sizeof(search);search.hRadio=radio.value;
        search.fReturnAuthenticated=search.fReturnRemembered=search.fReturnUnknown=TRUE;
        search.fIssueInquiry=TRUE;search.cTimeoutMultiplier=2;
        BLUETOOTH_DEVICE_INFO device{};device.dwSize=sizeof(device);
        HBLUETOOTH_DEVICE_FIND found=BluetoothFindFirstDevice(&search,&device);if(!found)continue;
        do{
            if(stop)break;
            if(wcscmp(device.szName,L"Nintendo RVL-CNT-01") && wcscmp(device.szName,L"Nintendo RVL-CNT-01-TR"))continue;
            if(device.fConnected)continue;
            if(!device.fAuthenticated){
                WCHAR pin[7]{};
                // Red SYNC pairs with the host address; 1+2 guest pairing uses the remote address.
                for(int i=0;i<6;++i)pin[i]=ri.address.rgBytes[i];
                DWORD paired=BluetoothAuthenticateDevice(nullptr,radio.value,&device,pin,6);
                if(paired!=ERROR_SUCCESS && !stop){for(int i=0;i<6;++i)pin[i]=device.Address.rgBytes[i];BluetoothAuthenticateDevice(nullptr,radio.value,&device,pin,6);}
            }
            if(!stop){const GUID hidService{0x00001124,0,0x1000,{0x80,0,0,0x80,0x5f,0x9b,0x34,0xfb}};
                BluetoothSetServiceState(radio.value,&device,&hidService,BLUETOOTH_SERVICE_ENABLE);}
        }while(BluetoothFindNextDevice(found,&device));
        BluetoothFindDeviceClose(found);
    }while(!stop && BluetoothFindNextRadio(radios,&raw));
    BluetoothFindRadioClose(radios);return radioFound;
}
class NativeBackend final:public Backend {
public:
    NativeBackend():worker([this]{Run();}){}
    ~NativeBackend(){stop=true;cancelInquiry=true;cv.notify_all();if(worker.joinable())worker.join();devices={};}
    void Enable(bool on,bool continuous)override{
        {std::lock_guard lock(mutex);enabled=on;continuous_=continuous;cancelInquiry=!on;if(!on)clear=true;}
        cv.notify_all();
    }
    void Scan()override{{std::lock_guard lock(mutex);requested=true;blocked.clear();}cv.notify_all();}
    void Disconnect(int p)override{
        if(p<0 || p>1)return;
        {std::lock_guard lock(mutex);disconnect[p]=true;}cv.notify_all();
    }
    void SwapPlayers()override{
        std::lock_guard lock(mutex);std::swap(devices[0],devices[1]);std::swap(paths[0],paths[1]);
        for(int i=0;i<2;++i)if(devices[i])devices[i]->player=i;
    }
    Snapshot Poll()override{
        std::lock_guard lock(mutex);Snapshot s;s.scanning=scanning;s.message=message;
        for(int i=0;i<2;++i){if(devices[i])s.player[i]=devices[i]->Poll();s.player[i].reserved=!paths[i].empty();}
        return s;
    }
private:
    std::atomic<bool> stop{false},cancelInquiry{false};std::mutex mutex;std::condition_variable cv;
    std::array<std::shared_ptr<Device>,2> devices;
    std::array<std::wstring,2> paths;
    std::array<bool,2> disconnect{};
    std::set<std::wstring> blocked;
    bool enabled=false,continuous_=false,requested=false,clear=false,scanning=false;
    std::string message="Select Connect and press the red SYNC button";
    std::thread worker;
    void Run(){
        auto deadline=Clock::now();
        while(!stop){
            std::array<std::shared_ptr<Device>,2> retired;
            bool scan=false;
            {
                std::unique_lock lock(mutex);
                cv.wait_for(lock,std::chrono::milliseconds(300),[&]{return stop || requested || clear || disconnect[0] || disconnect[1];});
                if(stop)break;
                for(int i=0;i<2;++i)if(clear || disconnect[i]){
                    if(disconnect[i] && !paths[i].empty())blocked.insert(paths[i]);
                    retired[i]=std::move(devices[i]);paths[i].clear();disconnect[i]=false;
                }
                clear=false;
                if(requested){deadline=Clock::now()+std::chrono::seconds(20);requested=false;}
                scan=enabled && (continuous_ || Clock::now()<deadline);
                scanning=scan;
                if(scan)message="Press red SYNC   DolphinBar uses Mode 4";
                else message=enabled?"Connect to search again   Sensor bar needed":"Wii Remote support is off";
            }
            retired={};
            if(!scan)continue;
            // Existing paired HID devices and DolphinBars are checked before a Bluetooth inquiry.
            for(const auto& path:HidPaths()){
                int slot=-1;
                {
                    std::lock_guard lock(mutex);if(!enabled || blocked.count(path))continue;
                    for(int i=0;i<2;++i)if(paths[i]==path && (!devices[i] || !devices[i]->alive)){slot=i;break;}
                    if(slot<0){bool known=paths[0]==path || paths[1]==path;if(known)continue;
                        for(int i=0;i<2;++i)if(paths[i].empty()){slot=i;break;}}
                }
                if(slot<0)continue;
                auto candidate=std::make_shared<Device>(path,slot);
                if(!candidate->Open())continue;
                std::shared_ptr<Device> previous;
                {std::lock_guard lock(mutex);if(enabled && !blocked.count(path) && (paths[slot].empty() || paths[slot]==path)){
                    previous=std::move(devices[slot]);devices[slot]=candidate;paths[slot]=path;}}
            }
            bool needRadio=false;
            {std::lock_guard lock(mutex);needRadio=enabled && ((!devices[0] || !devices[0]->alive)||(!devices[1] || !devices[1]->alive));}
            if(needRadio && !DiscoverBluetooth(cancelInquiry)){
                std::lock_guard lock(mutex);message="No Bluetooth radio   Connect a DolphinBar or adapter";
            }
            std::unique_lock lock(mutex);cv.wait_for(lock,std::chrono::seconds(1),[&]{return stop || requested || clear || disconnect[0] || disconnect[1];});
        }
    }
};
} // namespace
std::unique_ptr<Backend> CreateBackend(){return std::make_unique<NativeBackend>();}
} // namespace input::wii
#endif

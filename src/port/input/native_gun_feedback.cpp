#include "native_gun_feedback.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace input::feedback {

const char* GunTypeName(GunType type) {
    switch (type) {
    case GunType::OpenFire: return "OpenFIRE";
    case GunType::Gun4ir: return "GUN4IR";
    case GunType::Blamcon: return "Blamcon";
    case GunType::Sinden: return "Sinden";
    default: return "Off";
    }
}

std::string EncodeGunCommand(GunType type, GunCommand command) {
    // Protocol references (protocol only; no vendor source is included):
    // OpenFIRE: github.com/TeamOpenFIRE/OpenFIRE-Firmware/wiki/MAMEHOOKER-Documentation
    // GUN4IR: JB Game Lab, JB GUN4IR Manual v1.26 pp.20-21 (S/F/E commands).
    // Blamcon: blamcon.com/get-started-with-blamcon/serial-commands/
    // Count zero on OpenFIRE/GUN4IR pulses means "only if not already queued".
    if (type == GunType::OpenFire || type == GunType::Gun4ir) {
        switch (command) {
        // S6 explicitly hands all feedback to serial, preventing the gun's
        // trigger-driven recoil from also firing alongside game recoil. Unlike
        // separate S0/S1, it does not rely on modes accumulating in firmware.
        case GunCommand::Begin: return "S6\nF0x0\nF1x0\n";
        case GunCommand::Shot: return "F0x2x0\n";
        case GunCommand::Damage: return "F1x2x0\n";
        case GunCommand::Stop: return "F0x0\nF1x0\n";
        case GunCommand::End: return "F0x0\nF1x0\nE\n";
        }
    } else if (type == GunType::Blamcon) {
        switch (command) {
        case GunCommand::Begin: return "SM.0.1+SM.1.1+FB.0.0+FB.1.0+";
        case GunCommand::Shot: return "FB.0.1+";
        case GunCommand::Damage: return "FB.1.1+";
        case GunCommand::Stop: return "FB.0.0+FB.1.0+";
        case GunCommand::End: return "FB.0.0+FB.1.0+SM.0.0+SM.1.0+";
        }
    }
    return {};
}

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
bool SerialType(GunType type) {
    return type == GunType::OpenFire || type == GunType::Gun4ir || type == GunType::Blamcon;
}

class NativeSerialTransport final : public SerialTransport {
public:
    ~NativeSerialTransport() override { Close(); }
    bool Open(int port) override {
#ifdef _WIN32
        Close();
        if (port < 1 || port > 256) return false;
        const std::wstring path = L"\\\\.\\COM" + std::to_wstring(port);
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) return false;
        DCB dcb{}; dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(handle_, &dcb)) { Close(); return false; }
        dcb.BaudRate = CBR_9600; dcb.ByteSize = 8; dcb.Parity = NOPARITY; dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE; dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = FALSE; dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE; dcb.fDsrSensitivity = FALSE;
        dcb.fOutX = FALSE; dcb.fInX = FALSE; dcb.fRtsControl = RTS_CONTROL_DISABLE;
        COMMTIMEOUTS timeouts{}; timeouts.WriteTotalTimeoutConstant = 100;
        if (!SetCommState(handle_, &dcb) || !SetCommTimeouts(handle_, &timeouts)) {
            Close(); return false;
        }
        event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!event_) { Close(); return false; }
        return true;
#else
        (void)port;
        return false;
#endif
    }
    bool Write(std::string_view bytes) override {
#ifdef _WIN32
        if (handle_ == INVALID_HANDLE_VALUE || !event_ || bytes.empty() || bytes.size() > 128) return false;
        OVERLAPPED ov{}; ov.hEvent = event_; ResetEvent(event_);
        DWORD written = 0;
        BOOL ok = WriteFile(handle_, bytes.data(), static_cast<DWORD>(bytes.size()), &written, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(event_, 100) != WAIT_OBJECT_0) {
                CancelIoEx(handle_, &ov);
                GetOverlappedResult(handle_, &ov, &written, TRUE);
                return false;
            }
            ok = GetOverlappedResult(handle_, &ov, &written, FALSE);
        }
        return ok && written == bytes.size();
#else
        (void)bytes;
        return false;
#endif
    }
    void Close() override {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) { CancelIoEx(handle_, nullptr); CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; }
        if (event_) { CloseHandle(event_); event_ = nullptr; }
#endif
    }
private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    HANDLE event_ = nullptr;
#endif
};
} // namespace

class GunFeedback::Impl {
public:
    explicit Impl(SerialTransportFactory factory) : factory_(std::move(factory)) {
        if (!factory_) factory_ = [] { return std::make_unique<NativeSerialTransport>(); };
        worker_ = std::thread([this] { Run(); });
    }
    ~Impl() { Close(); }

    bool Configure(int slot, GunType type, int port) {
        if (slot < 0 || slot >= 2) return false;
        std::lock_guard lock(mutex_);
        if (closed_) return false;
        if (!SerialType(type)) { type = GunType::Off; port = 0; }
        auto& s = slots_[slot];
        if (s.type == type && s.port == port) return s.problem.empty();
        s.type = type; s.port = port;
        ++s.revision; s.shot = s.damage = false;
        // P1 retains a contested port. Revalidate both slots whenever either
        // changes, so a rejected P2 configuration automatically becomes usable
        // when P1 moves away (including swapping the two ports in the menu).
        for (int p = 0; p < 2; ++p) {
            auto& item = slots_[p];
            const bool valid = item.type == GunType::Off || (item.port >= 1 && item.port <= 256);
            const bool duplicate = p == 1 && item.type != GunType::Off && slots_[0].type != GunType::Off &&
                                   item.port >= 1 && item.port <= 256 && item.port == slots_[0].port;
            const std::string problem = !valid ? "Choose COM1 - COM256" : duplicate ? "COM port already assigned" : "";
            if (item.problem != problem) {
                item.problem = problem; ++item.revision; item.shot = item.damage = false;
                item.status = !problem.empty() ? problem : item.type == GunType::Off ? "Off" : active_ ? "Connecting" : "Inactive";
            } else if (p == slot) {
                item.status = !problem.empty() ? problem : item.type == GunType::Off ? "Off" : active_ ? "Connecting" : "Inactive";
            }
        }
        changed_.notify_one();
        return s.problem.empty();
    }
    void SetActive(bool active) {
        std::lock_guard lock(mutex_);
        if (closed_ || active_ == active) return;
        active_ = active;
        for (auto& s : slots_) {
            ++s.revision; s.shot = s.damage = false;
            if (s.problem.empty()) s.status = s.type == GunType::Off ? "Off" : active ? "Connecting" : "Stopping";
        }
        changed_.notify_one();
    }
    void Pulse(int slot, bool damage) {
        if (slot < 0 || slot >= 2) return;
        std::lock_guard lock(mutex_);
        auto& s = slots_[slot];
        if (closed_ || !active_ || s.type == GunType::Off || !s.problem.empty() || s.status != "Ready") return;
        if (damage) { s.damage = true; s.damageAt = Clock::now(); }
        else { s.shot = true; s.shotAt = Clock::now(); }
        changed_.notify_one();
    }
    std::string Status(int slot) const {
        if (slot < 0 || slot >= 2) return "Off";
        std::lock_guard lock(mutex_);
        return slots_[slot].status;
    }
    void Close() {
        {
            std::lock_guard lock(mutex_);
            if (closed_) return;
            closed_ = true; active_ = false;
            for (auto& s : slots_) { ++s.revision; s.shot = s.damage = false; }
        }
        changed_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

private:
    struct Slot {
        GunType type = GunType::Off;
        int port = 0;
        uint64_t revision = 1;
        bool shot = false, damage = false;
        Clock::time_point shotAt{}, damageAt{};
        std::string problem, status = "Off";
    };
    struct Session {
        std::unique_ptr<SerialTransport> transport;
        GunType type = GunType::Off;
        uint64_t revision = 0;
        bool handoff = false;
        Clock::time_point shotAt{}, damageAt{};
        void Close() {
            if (transport) {
                if (handoff) transport->Write(EncodeGunCommand(type, GunCommand::End));
                transport->Close(); transport.reset();
            }
            handoff = false; shotAt = damageAt = {};
        }
    };
    bool Current(int slot, uint64_t revision) const {
        std::lock_guard lock(mutex_);
        return !closed_ && active_ && slots_[slot].revision == revision;
    }
    void SetStatus(int slot, uint64_t revision, std::string value) {
        std::lock_guard lock(mutex_);
        if (slots_[slot].revision == revision) slots_[slot].status = std::move(value);
    }
    void Run() {
        std::array<Session, 2> sessions;
        for (;;) {
            std::array<Slot, 2> desired;
            bool active = false, closed = false;
            {
                std::lock_guard lock(mutex_);
                desired = slots_; active = active_; closed = closed_;
                for (auto& s : slots_) s.shot = s.damage = false;
            }
            if (closed) break;
            for (int p = 0; p < 2; ++p) {
                const auto& d = desired[p]; auto& session = sessions[p];
                if (session.revision != d.revision) {
                    session.Close(); session.revision = d.revision; session.type = d.type;
                    if (!d.problem.empty()) continue;
                    if (!active || d.type == GunType::Off) {
                        SetStatus(p, d.revision, d.type == GunType::Off ? "Off" : "Inactive");
                        continue;
                    }
                    SetStatus(p, d.revision, "Connecting");
                    session.transport = factory_();
                    if (!session.transport || !session.transport->Open(d.port)) {
                        session.Close(); SetStatus(p, d.revision, "COM" + std::to_string(d.port) + " unavailable");
                        continue;
                    }
                    if (!Current(p, d.revision)) { session.Close(); continue; }
                    // Mark handoff before writing: even a partial start gets an End attempt.
                    session.handoff = true;
                    if (!session.transport->Write(EncodeGunCommand(d.type, GunCommand::Begin))) {
                        session.Close(); SetStatus(p, d.revision, "Connection lost"); continue;
                    }
                    if (!Current(p, d.revision)) { session.Close(); continue; }
                    SetStatus(p, d.revision, "Ready");
                }
                if (!active || !session.transport || !session.handoff || !Current(p, d.revision)) continue;
                const auto now = Clock::now();
                auto pulse = [&](bool pending, Clock::time_point queued, Clock::time_point& last, auto interval, GunCommand command) {
                    if (!pending || now - queued > 100ms || (last != Clock::time_point{} && now - last < interval)) return true;
                    if (!Current(p, d.revision)) return true;
                    if (!session.transport->Write(EncodeGunCommand(d.type, command))) return false;
                    last = Clock::now(); return true;
                };
                if (!pulse(d.shot, d.shotAt, session.shotAt, 80ms, GunCommand::Shot) ||
                    !pulse(d.damage, d.damageAt, session.damageAt, 250ms, GunCommand::Damage)) {
                    session.Close(); SetStatus(p, d.revision, "Connection lost");
                }
            }
            std::unique_lock lock(mutex_);
            changed_.wait(lock, [&] {
                if (closed_) return true;
                for (int p = 0; p < 2; ++p)
                    if (slots_[p].revision != sessions[p].revision || slots_[p].shot || slots_[p].damage) return true;
                return false;
            });
        }
        for (auto& session : sessions) session.Close();
    }

    SerialTransportFactory factory_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::array<Slot, 2> slots_;
    bool active_ = false, closed_ = false;
    std::thread worker_;
};

GunFeedback::GunFeedback(SerialTransportFactory factory) : impl_(std::make_unique<Impl>(std::move(factory))) {}
GunFeedback::~GunFeedback() = default;
bool GunFeedback::Configure(int slot, GunType type, int port) { return impl_->Configure(slot, type, port); }
void GunFeedback::SetActive(bool active) { impl_->SetActive(active); }
void GunFeedback::Shot(int slot) { impl_->Pulse(slot, false); }
void GunFeedback::Damage(int slot) { impl_->Pulse(slot, true); }
void GunFeedback::Stop() { impl_->SetActive(false); }
void GunFeedback::Close() { impl_->Close(); }
std::string GunFeedback::Status(int slot) const { return impl_->Status(slot); }

} // namespace input::feedback

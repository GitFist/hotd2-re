#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cmath>

namespace port {
// DXGI accepts sync intervals1..4. Each generated/retained image must occupy
// exactly one120Hz slot, so240Hz needs Present(2),360Hz Present(3), etc.
inline int FrameGenerationVsyncInterval(float refreshHz) {
    if(!std::isfinite(refreshHz)||refreshHz<=0)return 0;
    const int interval=static_cast<int>(std::lround(refreshHz/120.f));
    if(interval<1||interval>4)return 0;
    return std::abs(refreshHz-interval*120.f)<=interval*.6f?interval:0;
}
// Presentation-only pacing for explicitly selected 60Hz -> 120Hz interpolation.
// This class never owns a simulation tick, input record or network clock.
class D3D12FramePacer {
public:
    D3D12FramePacer() {
        QueryPerformanceFrequency(&frequency_);
        timer_ = CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        if(!timer_)timer_=CreateWaitableTimerW(nullptr,FALSE,nullptr);
    }
    ~D3D12FramePacer(){if(timer_)CloseHandle(timer_);}
    int64_t Now() const {LARGE_INTEGER now{};QueryPerformanceCounter(&now);return now.QuadPart;}
    double ElapsedMs(int64_t since) const {return double(Now()-since)*1000/double(frequency_.QuadPart);}
    void WaitHalfFrame(int64_t generatedPresent) const {
        const int64_t deadline=generatedPresent+frequency_.QuadPart/120;
        int64_t remaining=deadline-Now();
        if(remaining<=0)return;
        if(timer_){
            LARGE_INTEGER due{};due.QuadPart=-std::max<int64_t>(1,remaining*10000000/frequency_.QuadPart);
            if(SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE))WaitForSingleObject(timer_,1000);
        }
        while(Now()<deadline)SwitchToThread();
    }
private:
    LARGE_INTEGER frequency_{};
    HANDLE timer_=nullptr;
};
} // namespace port
#endif

#pragma once
#include "gpu_timing.h"
#include <algorithm>
#include <cmath>

namespace port {
// Render-only resolution ladder. The caller supplies only active gameplay GPU
// samples; simulation/network/loading wall time is deliberately absent here.
class GpuBudgetController {
public:
    bool Configure(double targetMilliseconds,float minimum=.5f,float maximum=1.f) {
        if(!std::isfinite(targetMilliseconds)||targetMilliseconds<2||targetMilliseconds>50||
           !std::isfinite(minimum)||!std::isfinite(maximum)||minimum<.25f||maximum>1||minimum>maximum)return false;
        target_=targetMilliseconds;minimum_=minimum;maximum_=maximum;Reset(maximum);return true;
    }
    void Reset(float scale=1.f) {scale_=std::clamp(scale,minimum_,maximum_);average_=0;serial_=0;count_=over_=under_=cooldown_=0;}
    float Scale() const{return scale_;}
    double AverageGpuMilliseconds() const{return average_;}
    bool Observe(const GpuTimingSample& sample,int currentWidth,int currentHeight) {
        if(!sample.valid||sample.serial<=serial_||sample.width!=currentWidth||sample.height!=currentHeight||
           !std::isfinite(sample.milliseconds)||sample.milliseconds<=0||sample.milliseconds>500)return false;
        serial_=sample.serial;average_=count_?average_*.8+sample.milliseconds*.2:sample.milliseconds;++count_;
        if(cooldown_){--cooldown_;return false;}
        if(count_<8)return false;
        over_=average_>target_*1.08?over_+1:0;
        under_=average_<target_*.75?under_+1:0;
        float next=scale_;
        if(over_>=3)next=std::max(minimum_,scale_-1.f/16);
        else if(under_>=45)next=std::min(maximum_,scale_+1.f/16);
        if(next==scale_)return false;
        scale_=next;over_=under_=0;cooldown_=12;return true;
    }
private:
    double target_=1000./60,average_=0;
    float minimum_=.5f,maximum_=1,scale_=1;
    uint64_t serial_=0;
    unsigned count_=0,over_=0,under_=0,cooldown_=0;
};
} // namespace port

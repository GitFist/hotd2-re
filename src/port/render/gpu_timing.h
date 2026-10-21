#pragma once
#include <cstdint>
namespace port {
// Actual GPU timestamp interval, never CPU frame time or Present wait. Samples
// carry render dimensions so resolution changes can reject in-flight old data.
struct GpuTimingSample {
    uint64_t serial=0;
    int width=0,height=0;
    double milliseconds=0;
    bool valid=false;
};
} // namespace port

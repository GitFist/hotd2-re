#pragma once
#include <cstdint>

namespace port {
// Actual cached objects created by a between-frame warmup, not estimated work
// or a promise that every driver/asset first-use stall has been eliminated.
struct GraphicsWarmupStats {
    uint32_t pipelinesCreated=0,stateObjectsCreated=0,samplersCreated=0;
    uint32_t pipelinesResident=0,stateObjectsResident=0,samplersResident=0;
    double cpuMilliseconds=0;
};
} // namespace port

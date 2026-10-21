#pragma once

#include "port/render/native_d3d12_interop.h"

#ifdef _WIN32
namespace play {
struct ReconstructionFallbackResult {
    bool detached = false, outputRestored = false;
    std::string notice;
};

// Between frames, after the failed SDK submission has completed. SDK faults are
// latched until restart; this does not change the user's requested preferences.
// If the full output allocation also fails, keep the previous raster target and
// present it without temporal jitter rather than retrying the SDK every frame.
inline ReconstructionFallbackResult RestoreNativeReconstruction(port::GpuDevice& gpu, int outputWidth, int outputHeight,
                                                                 const std::string& reason) {
    ReconstructionFallbackResult result;
    result.detached = port::SetNativeD3D12FrameCallback(gpu, nullptr, nullptr);
    if (!result.detached) {
        result.notice = "Reconstruction recovery failed: the active renderer is not native Direct3D 12.";
        return result;
    }
    result.outputRestored = (gpu.TargetWidth() == outputWidth && gpu.TargetHeight() == outputHeight) ||
                            gpu.ResizeTarget(outputWidth, outputHeight);
    result.notice = "Reconstruction unavailable until restart: " + reason;
    if (result.outputRestored)
        result.notice += ". Native output restored.";
    else
        result.notice += ". Output allocation failed; stable native " + std::to_string(gpu.TargetWidth()) + "x" +
                         std::to_string(gpu.TargetHeight()) + " retained.";
    return result;
}
}  // namespace play
#endif

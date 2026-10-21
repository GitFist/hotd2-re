#pragma once
#include "graphics_options.h"
#include <cstdint>

namespace port {
// Spatial scene filtering happens once before explicit UI. Display-wide color,
// scanlines, CRT geometry/phosphor and scRGB conversion happen once afterward.
inline GraphicsOptions HudSceneGraphics(const GraphicsOptions& graphics,bool fsr) {
    GraphicsOptions scene;
    scene.postAA=graphics.postAA==1?1:0;
    scene.sharpening=fsr?0:graphics.sharpening;
    scene.downsample=graphics.downsample;
    scene.bloom=graphics.bloom;
    return scene;
}
inline GraphicsOptions HudDisplayGraphics(GraphicsOptions graphics) {
    graphics.postAA=0;graphics.spatialUpscaler=0;graphics.sharpening=0;
    graphics.downsample=false;graphics.bloom=0;
    return graphics;
}
inline bool ValidHudOutputSize(int width,int height) {
    return width>0&&height>0&&width<=8192&&height<=8192&&
        static_cast<uint64_t>(width)*height<=16u*1024*1024;
}
} // namespace port

#pragma once
#include <algorithm>
#include "render/viewport.h"
#include "input/aim_map.h"

namespace play {
inline float DisplayViewportAspect(int setting, int width, int height) {
    switch (setting) {
    case 1: return 16.f/9.f;
    case 2: return 21.f/9.f;
    case 3: return 32.f/9.f;
    case 4: return port::ViewportAspect(float(std::max(width,1))/std::max(height,1));
    default: return port::kNativeAspect;
    }
}
// Native menu/HUD coordinates occupy a centered 4:3 portion of the wider view.
inline float MenuPointerX(float normalizedX, float aspect) {
    return .5f+(normalizedX-.5f)/port::ViewportXScale(aspect);
}
inline input::ImageRect DisplayImageRect(int ww,int wh,float aspect,bool stretch,bool integer) {
    if(stretch)return input::FitImage(ww,wh,input::Aspect::Stretch);
    if(integer && aspect==port::kNativeAspect)return input::FitIntegerImage(ww,wh);
    return input::FitImageAspect(ww,wh,aspect);
}
} // namespace play

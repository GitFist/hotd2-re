#pragma once

#include <string>

namespace play {
// A saved optional backend must not lock the player out of the settings screen.
// The factory is shared by production and the failure-policy test; explicit CLI
// requests remain fatal when permitFallback is false. No preference is mutated.
template <class Renderer, class Factory>
auto CreateRequestedRenderer(Renderer requested, Renderer fallback, bool permitFallback, Factory&& factory,
                             bool* usedFallback, std::string* error, std::string* notice) {
    if (usedFallback) *usedFallback = false;
    std::string firstError;
    auto device = factory(requested, &firstError);
    if (device) {
        if (error) error->clear();
        return device;
    }
    if (!permitFallback || requested == fallback) {
        if (error) *error = firstError;
        return device;
    }
    std::string fallbackError;
    device = factory(fallback, &fallbackError);
    if (device) {
        if (usedFallback) *usedFallback = true;
        if (error) error->clear();
        if (notice) *notice = "Saved renderer unavailable: " + firstError + ". Native fallback active; saved choices retained.";
    } else if (error)
        *error = "Saved renderer failed: " + firstError + ". Native fallback also failed: " + fallbackError;
    return device;
}
}  // namespace play

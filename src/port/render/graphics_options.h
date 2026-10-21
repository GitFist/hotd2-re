#pragma once

#include <string>

namespace port {

// Cosmetic options only: these never change the fixed 60 Hz simulation or network state.
// Defaults preserve Build Z's authored filtering, single-sample target and present pass.
struct GraphicsOptions {
    int msaa = 1;             // 1 (Off), 2, 4 samples
    int anisotropy = 1;       // 1 (Off), 2, 4, 8, 16; minified model materials only
    int textureFilter = 0;    // 0 authored, 1 nearest magnification, 2 linear magnification (3D only)
    int postAA = 0;           // 0 Off, 1 FXAA, 2 SMAA1x, 3 Intel CMAA2 (final image)
    bool temporalAA = false; // validated geometry/depth history, resolves before HUD
    bool outputResolutionHud = false; // explicit HUD at present-image size after scene reconstruction
    int spatialUpscaler = 0;  // 0 sharp bilinear, 1 AMD FSR1 EASU + optional RCAS
    int sharpening = 0;       // 0 Off, 1..3 bounded unsharp mask
    bool alphaToCoverage = false; // MSAA alpha-tested, non-blended 3D materials only
    int syncInterval = 0;     // 0 unsynchronized, 1 VSync; actual SDL fallback is reported
    bool allowTearing = false; // VRR-eligible unsynchronized presentation when supported
    bool lowLatency = false;   // cap queued frames; skip drawing while presentation is busy
    bool hdrOutput = false;    // FP16 linear scRGB output; original art retains its SDR range
    int hdrPaperWhite = 200;   // cd/m2, 80..400
    bool downsample = false; // area-filter source pixels when internal resolution exceeds output
    int brightness = 50;      // 0..100; 50 neutral
    int gamma = 50;           // 0..100; 50 neutral
    int contrast = 50;        // 0..100; 50 neutral
    int saturation = 50;      // 0..100; 50 neutral
    int bloom = 0;            // 0 Off, 1..3 SDR bright-area glow
    int colorGrade = 0;       // 0 original, 1 warm, 2 cool, 3 monochrome
    int crtMask = 0;          // 0 Off, 1 aperture grille, 2 slot mask, 3 shadow mask
    int crtCurvature = 0;     // 0 Off, 1..3 barrel geometry; input uses matching CrtWarpUV
    int crtGlow = 0;          // 0 Off, 1..3 bounded phosphor glow
    bool perPixelLighting = false;
    int softParticles = 0;      // 0 Off, 1..3 smoke intersection fade distance
    bool materialMaps = false;  // manifest-opted tangent normals / roughness on opaque surfaces
    int ambientOcclusion = 0;   // 0 Off, 1..3 opaque-only screen-space contact shading
    int shadowMaps = 0;         // 0 Off, 1..3 directional raster map resolution
    int volumetricFog = 0;     // 0 Off, 1..3 depth-integrated shadowed haze
    int reflections = 0;       // 0 Off, 1..3 material-gated screen-space rays
    int cinematicFocus = 0;    // 0 Off, 1..3 distant blur only in noninteractive cutscenes
    int meshRefinement = 0;    // 0 Off, 1..3 subdivision on pack-selected visual models
    bool operator==(const GraphicsOptions&) const = default;
};

struct GraphicsCapabilities {
    bool msaa2 = false, msaa4 = false;
    bool temporalAA = false;
    bool outputResolutionHud = false;
    bool cmaa2 = false;
    bool gpuTiming = false;
    bool pipelineWarmup = false;
    bool sceneEffects = false;
    bool smaa = false,spatialFSR1 = false,softParticles = false,materialMaps = false,ambientOcclusion = false,shadowMaps = false;
    int maxAnisotropy = 1;
    bool tearing = false;
    bool lowLatency = false;
    bool hdrOutput = false;
};

inline bool ValidateGraphicsOptions(const GraphicsOptions& g, std::string* error = nullptr) {
    const bool valid = (g.msaa == 1 || g.msaa == 2 || g.msaa == 4) &&
        (g.anisotropy == 1 || g.anisotropy == 2 || g.anisotropy == 4 || g.anisotropy == 8 || g.anisotropy == 16) &&
        g.textureFilter >= 0 && g.textureFilter <= 2 && g.postAA >= 0 && g.postAA <= 3 &&
        g.spatialUpscaler >= 0 && g.spatialUpscaler <= 1 &&
        g.hdrPaperWhite >= 80 && g.hdrPaperWhite <= 400 &&
        g.sharpening >= 0 && g.sharpening <= 3 && g.syncInterval >= 0 && g.syncInterval <= 1 &&
        g.brightness >= 0 && g.brightness <= 100 && g.gamma >= 0 && g.gamma <= 100 &&
        g.contrast >= 0 && g.contrast <= 100 && g.saturation >= 0 && g.saturation <= 100 &&
        g.bloom >= 0 && g.bloom <= 3 && g.colorGrade >= 0 && g.colorGrade <= 3 &&
        g.crtMask >= 0 && g.crtMask <= 3 && g.crtCurvature >= 0 && g.crtCurvature <= 3 && g.crtGlow >= 0 && g.crtGlow <= 3 &&
        g.softParticles >= 0 && g.softParticles <= 3 && g.ambientOcclusion >= 0 && g.ambientOcclusion <= 3 &&
        g.shadowMaps >= 0 && g.shadowMaps <= 3 &&
        g.volumetricFog >= 0 && g.volumetricFog <= 3 && g.reflections >= 0 && g.reflections <= 3 &&
        g.cinematicFocus >= 0 && g.cinematicFocus <= 3 && g.meshRefinement >= 0 && g.meshRefinement <= 3;
    if (!valid && error) *error = "Invalid graphics option value";
    return valid;
}

// Matches kPresentShaderHlsl. Shared layout avoids subtly different presentation on the two backends.
struct PresentConstants {
    float src[4];
    float dst[4];
    float effects[4]; // FXAA, sharpening strength, quality downsample, bloom strength
    float color[4];   // brightness offset, gamma exponent, contrast, saturation
    float grade[4];   // grade index, unused
    float hdr[4];     // scRGB enabled, paper-white/80, reserved
    float crt[4];     // mask preset, curvature coefficient, glow strength, reserved
};

inline PresentConstants MakePresentConstants(int width, int height, int outWidth, int outHeight, int crt,
                                             const GraphicsOptions& g) {
    PresentConstants c{};
    c.src[0] = static_cast<float>(width); c.src[1] = static_cast<float>(height);
    c.src[2] = 1.0f / width; c.src[3] = 1.0f / height;
    c.dst[0] = static_cast<float>(outWidth); c.dst[1] = static_cast<float>(outHeight);
    c.dst[2] = static_cast<float>(crt); c.dst[3] = 480.0f;
    c.effects[0] = g.postAA == 1 ? 1.0f : 0.0f; c.effects[1] = g.sharpening * 0.2f;
    c.effects[2] = g.downsample ? 1.0f : 0.0f; c.effects[3] = g.bloom * 0.15f;
    c.color[0] = (g.brightness - 50) / 250.0f;
    // Positive gamma lifts midtones while preserving black/white endpoints.
    c.color[1] = g.gamma >= 50 ? 1.0f - (g.gamma - 50) / 100.0f : 1.0f + (50 - g.gamma) / 50.0f;
    c.color[2] = 1.0f + (g.contrast - 50) / 100.0f;
    c.color[3] = g.saturation / 50.0f;
    c.grade[0] = static_cast<float>(g.colorGrade);
    c.hdr[0] = g.hdrOutput ? 1.0f : 0.0f; c.hdr[1] = g.hdrPaperWhite / 80.0f;
    c.crt[0]=static_cast<float>(g.crtMask);c.crt[1]=g.crtCurvature*.035f;c.crt[2]=g.crtGlow*.18f;
    return c;
}

} // namespace port

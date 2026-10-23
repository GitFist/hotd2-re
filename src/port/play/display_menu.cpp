#include "port/play/display_menu.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "game/menu_audio.h"
#include "game/menu_text.h"

namespace play {
namespace {
constexpr uint32_t kSelected = 0x111, kNormal = 1;
constexpr float kResetLeft = 128, kResetRight = 304, kCancelLeft = 336, kCancelRight = 512;
constexpr float kResetTop = 278, kResetBottom = 316;
int Wrap(int v, int n) { return ((v % n) + n) % n; }
template <size_t N>
int Cycle(int v, const int (&a)[N], int delta) {
    auto it = std::find(std::begin(a), std::end(a), v);
    return a[Wrap(static_cast<int>(it == std::end(a) ? 0 : it - std::begin(a)) + delta, static_cast<int>(N))];
}
const char* OnOff(bool on) { return on ? "On" : "Off"; }
const char* Strength(int level) {
    const char* values[] = {"Off", "Low", "Medium", "High"};
    return values[std::clamp(level, 0, 3)];
}
auto ImageChoices(const port::GraphicsOptions& g) {
    return std::make_tuple(g.msaa, g.anisotropy, g.textureFilter, g.postAA, g.temporalAA, g.outputResolutionHud, g.sharpening,
                           g.alphaToCoverage, g.downsample);
}
const char* const kPages[] = {"Display", "Graphics", "Gameplay", "Sound", "Devices", "CRT", "Packs"};
constexpr float kTabWidths[] = {83, 91, 92, 75, 90, 61, 97};
constexpr int kContentTop = 126, kContentBottom = 420, kRowHeight = 26, kGroupHeight = 20, kBackTop = 430;
constexpr int kScrollButtonHeight = 32;
constexpr float kPreviewTextX = 558, kPreviewTextY = 462, kPreviewTextScale = .3f;
const char* GroupStart(DisplayMenu::Page page, int row) {
    using D = DisplayMenu;
    if (page == D::kDisplay) {
        if (row == D::kMonitor) return "SCREEN";
        if (row == D::kVsync) return "FRAME DELIVERY";
        if (row == D::kHdrOutput) return "CALIBRATION";
        if (row == D::kMenuStyle) return "INTERFACE";
    } else if (page == D::kGraphics) {
        if (row == D::kRenderer) return "RENDERING & RESOLUTION";
        if (row == D::kPreset) return "IMAGE CLARITY";
        if (row == D::kPerPixel) return "LIGHTING & EFFECTS";
        if (row == D::kContrast) return "COLOR";
        if (row == D::kStatistics) return "DIAGNOSTICS";
    } else if (page == D::kGameplay) {
        if (row == D::kDifficulty) return "NEW GAME RULES";
        if (row == D::kCrosshairP1) return "SIGHTS & APPEARANCE";
    } else if (page == D::kSound && row == D::kMasterVolume) return "VOLUME";
    else if (page == D::kDevices && row == D::kLightguns) return "SETUP & CALIBRATION";
    else if (page == D::kRetro && row == D::kCrt) return "CRT PRESENTATION";
    else if (page == D::kPacks && row == D::kPack) return "INSTALLED CONTENT";
    return "";
}
int ContentRowTop(int row, DisplayMenu::Page page) {
    if (row == DisplayMenu::kBack) return kBackTop;
    int top = kContentTop;
    for (int item : DisplayMenu::Rows(page)) {
        if (item == DisplayMenu::kBack) break;
        if (*GroupStart(page, item)) top += kGroupHeight;
        if (item == row) return top;
        top += kRowHeight;
    }
    return -10000;
}
int MaxScroll(DisplayMenu::Page page) {
    int bottom = kContentTop;
    for (int row : DisplayMenu::Rows(page))
        if (row != DisplayMenu::kBack) bottom = ContentRowTop(row, page) + 24;
    return std::max(0, bottom - kContentBottom);
}
const char* SectionAt(DisplayMenu::Page page, int row) {
    const char* section = "";
    for (int item : DisplayMenu::Rows(page)) {
        if (*GroupStart(page, item)) section = GroupStart(page, item);
        if (item == row) break;
    }
    return section;
}
int VolumeIndex(int row) {
    switch (row) {
        case DisplayMenu::kMusicVolume: return 0;
        case DisplayMenu::kGunVolume: return 1;
        case DisplayMenu::kEffectsVolume: return 2;
        case DisplayMenu::kMasterVolume: return 3;
        case DisplayMenu::kAttractionVolume: return 4;
        default: return -1;
    }
}
bool IsAction(int row) {
    return row == DisplayMenu::kResetGraphics || row == DisplayMenu::kResetGameplay || row == DisplayMenu::kLightguns ||
           row == DisplayMenu::kWiiRemotes || row == DisplayMenu::kCalibration;
}
const char* Help(int r) {
    switch (r) {
        case DisplayMenu::kFullscreen:
            return "Windowed, borderless desktop, or exclusive fullscreen at desktop resolution.";
        case DisplayMenu::kScale:
            return "Window size. Fit keeps the picture inside the desktop.";
        case DisplayMenu::kAspect:
            return "Fit keeps the selected aspect ratio with bars where needed. Stretch fills the window and distorts the image.";
        case DisplayMenu::kViewport:
            return "Choose the scene's aspect ratio. Wider views reveal more scenery and remain experimental.";
        case DisplayMenu::kRender:
            return "Higher internal resolutions use more GPU time and memory.";
        case DisplayMenu::kInteger:
            return "Whole-number 640x480 fit. Small windows use normal fit.";
        case DisplayMenu::kPreset:
            return "Changes image clarity and downsampling only. Presets adapt to the active renderer's supported features.";
        case DisplayMenu::kMsaa:
            return "Smooth polygon edges. 4x uses more GPU memory and time.";
        case DisplayMenu::kPostAA:
            return "FXAA, SMAA or CMAA2 smooth the finished scene. Can combine with MSAA/TAA. Sharp HUD keeps text after AA.";
        case DisplayMenu::kTemporalAA:
            return "Accumulate the moving scene with depth and motion validation. HUD stays sharp.";
        case DisplayMenu::kUpscaler:
            return "Spatial upscaling when render pixels are below the window size. Separate from vendor upscaling; no motion "
                   "history.";
        case DisplayMenu::kAniso:
            return "Clearer angled floors and walls. Native magnification stays.";
        case DisplayMenu::kFiltering:
            return "Magnification for 3D textures. Sprites stay native.";
        case DisplayMenu::kDownsample:
            return "Area-filter higher render resolutions into the output picture.";
        case DisplayMenu::kSharpen:
            return "Detail strength, not resolution. Sharp HUD keeps text after sharpening. Off preserves native detail.";
        case DisplayMenu::kCoverage:
            return "Coverage for opaque alpha cutouts. Needs MSAA 2x or 4x.";
        case DisplayMenu::kPerPixel:
            return "Smooth lighting on opaque models. Mapped materials always use pixel lighting; alpha effects stay native.";
        case DisplayMenu::kSoftParticles:
            return "Fade supported smoke where it meets opaque geometry.";
        case DisplayMenu::kMaterialMaps:
            return "Uses normal and roughness maps from the currently loaded pack. Selecting Next Launch does not load a pack yet.";
        case DisplayMenu::kAmbientOcclusion:
            return "Depth-based contact shading on opaque surfaces. Costs GPU time.";
        case DisplayMenu::kShadowMaps:
            return "Directional raster shadows at 1024, 2048 or 4096 pixels. Enables pixel lighting; separate from experimental Ray "
                   "Shadows.";
        case DisplayMenu::kHdrOutput:
            return "Linear scRGB output needs Windows HDR enabled. Original art remains SDR.";
        case DisplayMenu::kPaperWhite:
            return "Brightness of SDR white in HDR output, in nits. Original art has no authored HDR highlights.";
        case DisplayMenu::kFog:
            return "Depth-integrated haze and shadowed light shafts. Keep targets visible.";
        case DisplayMenu::kReflections:
            return "Screen-space reflections on explicit smooth material receivers.";
        case DisplayMenu::kCinematicFocus:
            return "Distant blur only during noninteractive cutscenes; aiming stays clear.";
        case DisplayMenu::kResolutionMode:
            return "Fixed Scale uses Render Scale. Match Window uses output pixels. SSAA renders 1.5x or 2x each output dimension.";
        case DisplayMenu::kDynamicResolution:
            return "GPU-timed resolution. Fixed-input upscalers and ray-shadow fence mode use stable resolution.";
        case DisplayMenu::kGpuBudget:
            return "Target for dynamic resolution, separate from Frame Limit. CPU, loading and network waits do not lower resolution.";
        case DisplayMenu::kCrtMask:
            return "Independent phosphor patterns. Inspect at native output pixels.";
        case DisplayMenu::kCrtCurvature:
            return "Barrel geometry with matching aim mapping. Disabled for raw gun input and frame generation.";
        case DisplayMenu::kCrtGlow:
            return "Bounded phosphor glow. Off preserves native contrast.";
        case DisplayMenu::kCalibration:
            return "Full-screen black, white, gamma and geometry reference patterns.";
        case DisplayMenu::kRenderer:
            return "Restart required. Direct3D 12 enables optional vendor reconstruction on Windows x64.";
        case DisplayMenu::kTemporalUpscaler:
            return "Restart required. Genuine vendor SDK; unavailable GPUs keep native rendering.";
        case DisplayMenu::kTemporalQuality:
            return "Quality trades render pixels for speed. Native AA uses full input pixels and is unavailable with FSR 2. Restart "
                   "required.";
        case DisplayMenu::kFrameGeneration:
            return "Restart required. DLSS / FSR FG targets 60 rendered inputs/second; generated images never step the game.";
        case DisplayMenu::kRenderInterpolation:
            return "Smooth render-only poses above 60 FPS. Adds up to one 60Hz tick of visual delay; aiming stays immediate.";
        case DisplayMenu::kRayShadows:
            return "Needs Direct3D 12, DXR1.1 and the installed compiler. Extra render cost; dynamic resolution stays Off.";
        case DisplayMenu::kIndependentUi:
            return "Keep explicit HUD at output pixels. Scene AA, bloom and sharpening happen before it; world effects stay in depth "
                   "order.";
        case DisplayMenu::kCrosshairs:
            return "Show both players' sights on this computer. Menu and calibration pointers stay visible. Aiming and shooting unchanged; "
                   "applies immediately.";
        case DisplayMenu::kMeshRefinement:
            return "Smooths selected models listed by the loaded pack. Higher levels add triangles. Collision and hit timing stay "
                   "original.";
        case DisplayMenu::kBloom:
            return "Optional SDR highlight glow. Off preserves original contrast.";
        case DisplayMenu::kBrightness:
            return "50 is neutral. Raise gently to retain dark-scene detail.";
        case DisplayMenu::kGamma:
            return "Midtone adjustment; 50 preserves the original curve.";
        case DisplayMenu::kContrast:
            return "50 is neutral. Strong contrast can hide scene detail.";
        case DisplayMenu::kSaturation:
            return "50 preserves original color; lower values reduce intensity.";
        case DisplayMenu::kColorGrade:
            return "Optional final color treatment. Original keeps authored colors.";
        case DisplayMenu::kVsync:
            return "Synchronize presentation to reduce tearing. Game stays at 60 Hz.";
        case DisplayMenu::kTearing:
            return "VRR-compatible presentation with VSync Off. Enable VRR in the driver.";
        case DisplayMenu::kLowLatency:
            return "Limit queued frames. Game and network polling continue while waiting.";
        case DisplayMenu::kFrameCap:
            return "Caps presented frames. Game logic and network polling continue.";
        case DisplayMenu::kMonitor:
            return "Move to a display. Recheck calibrated gun aim after moving.";
        case DisplayMenu::kStatistics:
            return "Presented FPS and frame time are separate from game ticks.";
        case DisplayMenu::kMenuStyle:
            return "Night Shift or previous menu. Applies on next launch.";
        case DisplayMenu::kMenuOpacity:
            return "Night Shift background strength. Lower values reveal more of the scene; text stays readable. H hides the graphics UI.";
        case DisplayMenu::kResetGraphics:
            return "Reset all display and graphics defaults: renderer, upscaling, frame generation, ray shadows and menu opacity. "
                   "Keeps gameplay, sound, packs and menu theme.";
        case DisplayMenu::kPack:
            return "Saved content pack for next launch; may include textures, materials, model rules and audio. No download.";
        case DisplayMenu::kCrt:
            return "Scanlines follow the original 480 game rows.";
        case DisplayMenu::kBlood:
            return "Local visual preference. Gameplay stays the same.";
        case DisplayMenu::kDifficulty:
            return "Enemy difficulty for your next local run. Current runs keep their existing rules.";
        case DisplayMenu::kLives:
            return "Starting lives for your next local run. This does not refill lives in a running game.";
        case DisplayMenu::kContinues:
            return "Continue credits for your next local run. Free Play remains tied to the original unlock.";
        case DisplayMenu::kCrosshairP1:
        case DisplayMenu::kCrosshairP2:
            return "Choose this player's original sight design. Gameplay Crosshairs hides both players' sights.";
        case DisplayMenu::kResetGameplay:
            return "Restore difficulty, starting lives, continues, sight styles and local appearance. Keeps records, unlocks and calibration.";
        case DisplayMenu::kMasterVolume:
            return "Overall volume. Scales music, shots, effects, dialogue and the menu attraction soundtrack.";
        case DisplayMenu::kMusicVolume:
            return "Gameplay music level. Master Volume also applies.";
        case DisplayMenu::kGunVolume:
            return "Player gunshot level. Master Volume also applies.";
        case DisplayMenu::kEffectsVolume:
            return "Sound effects, dialogue and menu feedback level. Master Volume also applies.";
        case DisplayMenu::kAttractionVolume:
            return "The standby gameplay soundtrack behind the main menu. Master Volume also applies.";
        case DisplayMenu::kLightguns:
            return "Open lightgun assignment, input mapping, calibration and the two-player gun test.";
        case DisplayMenu::kWiiRemotes:
            return "Open Wii Remote player slots, button binding, calibration and aiming tests.";
        default:
            return "Back returns to the previous menu.";
    }
}
}  // namespace
const std::vector<int>& DisplayMenu::Rows(Page p) {
    static const std::vector<int> rows[] = {
        {kMonitor, kFullscreen, kScale, kViewport, kAspect, kInteger, kVsync, kFrameCap, kTearing, kLowLatency,
         kHdrOutput, kPaperWhite, kBrightness, kGamma, kCalibration, kMenuStyle, kMenuOpacity, kResetGraphics, kBack},
        {kRenderer, kResolutionMode, kRender, kDynamicResolution, kGpuBudget, kTemporalUpscaler, kTemporalQuality,
         kUpscaler, kFrameGeneration, kRenderInterpolation, kPreset, kMsaa, kPostAA, kTemporalAA, kAniso, kFiltering,
         kDownsample, kSharpen, kCoverage, kIndependentUi, kPerPixel, kSoftParticles, kAmbientOcclusion, kShadowMaps,
         kRayShadows, kBloom, kFog, kReflections, kCinematicFocus, kContrast, kSaturation, kColorGrade, kStatistics, kBack},
        {kDifficulty, kLives, kContinues, kCrosshairP1, kCrosshairP2, kCrosshairs, kBlood, kResetGameplay, kBack},
        {kMasterVolume, kMusicVolume, kGunVolume, kEffectsVolume, kAttractionVolume, kBack},
        {kLightguns, kWiiRemotes, kBack},
        {kCrt, kCrtMask, kCrtCurvature, kCrtGlow, kBack},
        {kPack, kMaterialMaps, kMeshRefinement, kBack}};
    return rows[p >= 0 && p < kPageCount ? p : kDisplay];
}
void DisplayMenu::Open() {
    open_ = true;
    uiHidden_ = false;
    resetConfirm_ = false;
    resetGameplayConfirm_ = false;
    resetYes_ = false;
    optionChanged_ = false;
    gameplayReset_ = false;
    deviceAction_ = DeviceAction::None;
    page_ = kDisplay;
    cursor_ = Rows(page_).front();
    scroll_ = 0;
    pointer_ = {};
    calibration_ = -1;
}
void DisplayMenu::SetPage(Page p) {
    resetConfirm_ = false;
    resetGameplayConfirm_ = false;
    page_ = static_cast<Page>(std::clamp(static_cast<int>(p), 0, kPageCount - 1));
    cursor_ = Rows(page_).front();
    scroll_ = 0;
    pointer_ = {};
}
void DisplayMenu::FocusRow(int row) {
    const auto& rows = Rows(page_);
    if (std::find(rows.begin(), rows.end(), row) == rows.end()) return;
    cursor_ = row;
    KeepCursorVisible();
    pointer_ = {};
}
void DisplayMenu::KeepCursorVisible() {
    if (cursor_ == kBack) return;
    const int top = ContentRowTop(cursor_, page_);
    if (top - scroll_ < kContentTop) scroll_ = top - kContentTop;
    if (top + 24 - scroll_ > kContentBottom) scroll_ = top + 24 - kContentBottom;
    scroll_ = std::clamp(scroll_, 0, MaxScroll(page_));
}
game::MenuScrollHint DisplayMenu::ScrollHint() const {
    game::MenuScrollHint hint{26, kContentTop, 614, kContentBottom};
    if (open_ && !resetConfirm_ && !uiHidden_ && calibration_ < 0) {
        hint.above = scroll_ > 0;
        hint.below = scroll_ < MaxScroll(page_);
    }
    return hint;
}
std::vector<int> DisplayMenu::VisibleRows() const {
    std::vector<int> visible;
    for (int row : Rows(page_)) {
        const int top = ContentRowTop(row, page_) - (row == kBack ? 0 : scroll_);
        if (row == kBack || (top >= kContentTop && top + 24 <= kContentBottom)) visible.push_back(row);
    }
    return visible;
}
float DisplayMenu::RowY(int r, Page p, int scroll) {
    const int top = ContentRowTop(r, p) - (r == kBack ? 0 : scroll);
    return (float(top) - 10.f) / 24.f;
}
int DisplayMenu::RowAt(float x, float y, Page p, int scroll) {
    if (x < 26 || x >= 614) return -1;
    for (int r : Rows(p)) {
        const int top = ContentRowTop(r, p) - (r == kBack ? 0 : scroll);
        if ((r == kBack || (top >= kContentTop && top + 24 <= kContentBottom)) && y >= top && y < top + 24) return r;
    }
    return -1;
}
int DisplayMenu::TabAt(float x, float y) {
    if (y >= 78 && y < 111)
        for (int p = 0; p < kPageCount; ++p)
            if (x >= TabLeft(p) && x < TabLeft(p) + TabWidth(p)) return p;
    return -1;
}
float DisplayMenu::TabLeft(int page) {
    float left = 25;
    for (int p = 0; p < std::clamp(page, 0, static_cast<int>(kPageCount)); ++p) left += kTabWidths[p];
    return left;
}
float DisplayMenu::TabWidth(int page) { return page >= 0 && page < kPageCount ? kTabWidths[page] : 0; }
int DisplayMenu::ResetChoiceAt(float x, float y) {
    if (y < kResetTop || y >= kResetBottom) return -1;
    if (x >= kResetLeft && x < kResetRight) return 1;
    if (x >= kCancelLeft && x < kCancelRight) return 0;
    return -1;
}
void DisplayMenu::SetPacks(std::vector<PackChoice> packs, const std::string& path) {
    packs_ = std::move(packs);
    pack_ = 0;
    packChanged_ = false;
    for (size_t i = 0; i < packs_.size(); ++i)
        if (packs_[i].path == path) pack_ = static_cast<int>(i);
}
bool DisplayMenu::TakePackChange(std::string* path) {
    if (!packChanged_ || packs_.empty() || !path) return false;
    *path = packs_[pack_].path;
    packChanged_ = false;
    return true;
}
const char* DisplayMenu::Label(int r) {
    static const char* const names[kRowCount] = {"Display Mode",
                                                 "Window Scale",
                                                 "Image Fit",
                                                 "Scanlines",
                                                 "Render Scale",
                                                 "Blood Color",
                                                 "Back",
                                                 "Integer Scaling",
                                                 "Image Preset",
                                                 "Edge AA (MSAA)",
                                                 "Post AA",
                                                 "Anisotropic Filter",
                                                 "Texture Filter",
                                                 "Downsample",
                                                 "Sharpening",
                                                 "Alpha Coverage",
                                                 "Pixel Lighting",
                                                 "Bloom",
                                                 "Brightness",
                                                 "Gamma",
                                                 "Contrast",
                                                 "Saturation",
                                                 "Color Grade",
                                                 "VSync",
                                                 "Frame Limit",
                                                 "Monitor",
                                                 "Performance Stats",
                                                 "Menu Style",
                                                 "Next Launch",
                                                 "Allow Tearing",
                                                 "Low Latency",
                                                 "FSR 1 Upscaling",
                                                 "Aspect Ratio",
                                                 "Smoke Blending",
                                                 "Material Detail",
                                                 "Contact Shading",
                                                 "HDR Output",
                                                 "SDR White (HDR)",
                                                 "Motion AA (TAA)",
                                                 "Raster Shadows",
                                                 "Volumetric Fog",
                                                 "Reflections",
                                                 "Cutscene Blur",
                                                 "Resolution Mode",
                                                 "Dynamic Resolution",
                                                 "Dynamic FPS Target",
                                                 "Phosphor Mask",
                                                 "Curvature",
                                                 "Phosphor Glow",
                                                 "Test Patterns",
                                                 "Renderer",
                                                 "Upscaling",
                                                 "Upscale Quality",
                                                 "Frame Generation",
                                                 "Model Smoothing",
                                                 "Pose Interpolation",
                                                 "Sharp HUD",
                                                 "Ray Shadows",
                                                 "Menu Opacity",
                                                 "Reset All Graphics",
                                                 "Gameplay Crosshairs",
                                                 "Difficulty",
                                                 "Starting Lives",
                                                 "Continue Credits",
                                                 "Player 1 Sight",
                                                 "Player 2 Sight",
                                                 "Reset Gameplay",
                                                 "Master Volume",
                                                 "Music Volume",
                                                 "Gun Shots Volume",
                                                 "Effects + Dialogue",
                                                 "Attraction Volume",
                                                 "Light Guns",
                                                 "Wii Remotes"};
    return r >= 0 && r < kRowCount ? names[r] : "";
}
void DisplayMenu::ApplyPreset(DisplaySettings& d, int p, const port::GraphicsCapabilities* caps) {
    auto& g = d.graphics;
    const port::GraphicsOptions defaults;
    g.msaa = defaults.msaa;
    g.anisotropy = defaults.anisotropy;
    g.textureFilter = defaults.textureFilter;
    g.postAA = defaults.postAA;
    g.temporalAA = defaults.temporalAA;
    g.outputResolutionHud = defaults.outputResolutionHud;
    g.sharpening = defaults.sharpening;
    g.alphaToCoverage = defaults.alphaToCoverage;
    g.downsample = defaults.downsample;
    if (p > 0) {
        g.msaa = !caps || caps->msaa4 ? 4 : caps->msaa2 ? 2 : 1;
        g.anisotropy = caps ? std::clamp(caps->maxAnisotropy, 1, 16) : 16;
        g.downsample = true;
    }
    if (p > 1) {
        g.postAA = !caps || caps->smaa ? 2 : 1;
        g.sharpening = 1;
        g.outputResolutionHud = !caps || caps->outputResolutionHud;
    }
}
std::string DisplayMenu::PresetName(const DisplaySettings& d, const port::GraphicsCapabilities* caps) {
    for (int p = 0; p < 3; ++p) {
        auto c = d;
        ApplyPreset(c, p, caps);
        if (ImageChoices(c.graphics) == ImageChoices(d.graphics)) return p == 0 ? "Original" : p == 1 ? "Clean" : "Enhanced";
    }
    return "Custom";
}
std::string DisplayMenu::ValueText(const DisplaySettings& d, int r) {
    const auto& g = d.graphics;
    switch (r) {
        case kFullscreen:
            return !d.fullscreen ? "Windowed" : d.exclusiveFullscreen ? "Fullscreen" : "Borderless";
        case kScale:
            return d.fullscreen ? "" : d.windowScale ? std::to_string(d.windowScale) + "x" : "Fit";
        case kAspect:
            return d.stretch ? "Stretch" : "Fit";
        case kViewport: {
            static const char* names[] = {"Original 4:3", "16:9", "21:9", "32:9", "Window"};
            return names[std::clamp(d.viewport, 0, 4)];
        }
        case kCrt:
            return Strength(d.crt);
        case kRender:
            return std::to_string(d.renderScale) + "x";
        case kBlood:
            return d.redBlood ? "Red" : "Green";
        case kInteger:
            return OnOff(d.integerScale);
        case kPreset:
            return PresetName(d);
        case kMsaa:
            return g.msaa == 1 ? "Off" : std::to_string(g.msaa) + "x";
        case kPostAA:
            return g.postAA == 3 ? "CMAA2" : g.postAA == 2 ? "SMAA 1x" : g.postAA == 1 ? "FXAA" : "Off";
        case kUpscaler:
            return g.spatialUpscaler ? "FSR 1" : "Off";
        case kAniso:
            return g.anisotropy == 1 ? "Off" : std::to_string(g.anisotropy) + "x";
        case kFiltering:
            return g.textureFilter == 0 ? "Native" : g.textureFilter == 1 ? "Nearest" : "Linear";
        case kDownsample:
            return g.downsample ? "Area" : "Native";
        case kSharpen:
            return Strength(g.sharpening);
        case kCoverage:
            return OnOff(g.alphaToCoverage);
        case kPerPixel:
            return OnOff(g.perPixelLighting);
        case kSoftParticles: {
            const char* values[] = {"Off", "Narrow", "Medium", "Wide"};
            return values[std::clamp(g.softParticles, 0, 3)];
        }
        case kMaterialMaps:
            return OnOff(g.materialMaps);
        case kAmbientOcclusion:
            return Strength(g.ambientOcclusion);
        case kHdrOutput:
            return OnOff(g.hdrOutput);
        case kPaperWhite:
            return std::to_string(g.hdrPaperWhite) + " nits";
        case kTemporalAA:
            return OnOff(g.temporalAA);
        case kShadowMaps: {
            const char* values[] = {"Off", "1024", "2048", "4096"};
            return values[std::clamp(g.shadowMaps, 0, 3)];
        }
        case kFog:
            return Strength(g.volumetricFog);
        case kReflections:
            return Strength(g.reflections);
        case kCinematicFocus:
            return Strength(g.cinematicFocus);
        case kResolutionMode: {
            const char* values[] = {"Fixed Scale", "Match Window", "SSAA 150%", "SSAA 200%"};
            return values[std::clamp(d.resolutionMode, 0, 3)];
        }
        case kDynamicResolution:
            return OnOff(d.dynamicResolution);
        case kGpuBudget:
            return std::to_string(d.gpuBudgetFps) + " FPS";
        case kCrtMask: {
            const char* values[] = {"Off", "Aperture grille", "Slot mask", "Shadow mask"};
            return values[std::clamp(g.crtMask, 0, 3)];
        }
        case kCrtCurvature:
            return Strength(g.crtCurvature);
        case kCrtGlow:
            return Strength(g.crtGlow);
        case kCalibration:
            return "Open pattern";
        case kRenderer: {
            const char* values[] = {"Direct3D 11", "SDL GPU", "Direct3D 12"};
            return values[std::clamp(d.renderer, 0, 2)];
        }
        case kTemporalUpscaler: {
            const char* values[] = {"Off", "DLSS", "FSR 2", "FSR 3", "XeSS"};
            return values[std::clamp(d.temporalUpscaler, 0, 4)];
        }
        case kTemporalQuality: {
            const char* values[] = {"Quality", "Balanced", "Performance", "Native AA"};
            return values[std::clamp(d.temporalQuality, 0, 3)];
        }
        case kFrameGeneration:
            return d.frameGeneration == 2 ? "FSR 3 FG" : d.frameGeneration ? "DLSS FG" : "Off";
        case kRenderInterpolation:
            return OnOff(d.renderInterpolation);
        case kRayShadows:
            return OnOff(d.rayShadows);
        case kIndependentUi:
            return OnOff(g.outputResolutionHud);
        case kCrosshairs:
            return OnOff(d.showCrosshairs);
        case kMeshRefinement:
            return Strength(g.meshRefinement);
        case kBloom:
            return Strength(g.bloom);
        case kBrightness:
            return std::to_string(g.brightness);
        case kGamma:
            return std::to_string(g.gamma);
        case kContrast:
            return std::to_string(g.contrast);
        case kSaturation:
            return std::to_string(g.saturation);
        case kColorGrade:
            return g.colorGrade == 0 ? "Original" : g.colorGrade == 1 ? "Warm" : g.colorGrade == 2 ? "Cool" : "Mono";
        case kVsync:
            return OnOff(g.syncInterval != 0);
        case kTearing:
            return OnOff(g.allowTearing);
        case kLowLatency:
            return OnOff(g.lowLatency);
        case kFrameCap:
            return d.frameCap ? std::to_string(d.frameCap) + " FPS" : "Unlimited";
        case kMonitor:
            return d.monitor ? "Display " + std::to_string(d.monitor) : "Current window";
        case kStatistics:
            return OnOff(d.statistics);
        case kMenuStyle:
            return d.nightShift ? "Night Shift" : "Classic";
        case kMenuOpacity:
            return std::to_string(d.menuOpacity) + "%";
        default:
            return "";
    }
}
void DisplayMenu::Step(DisplaySettings& d, int r, int delta) {
    auto& g = d.graphics;
    const int samples[] = {1, 2, 4}, af[] = {1, 2, 4, 8, 16}, caps[] = {0, 30, 60, 120, 144, 165, 240}, scales[] = {1, 2, 3, 0};
    switch (r) {
        case kFullscreen: {
            if (!delta) break;
            const int mode = Wrap((d.fullscreen ? d.exclusiveFullscreen ? 2 : 1 : 0) + delta, 3);
            d.fullscreen = mode != 0;
            d.exclusiveFullscreen = mode == 2;
            break;
        }
        case kScale:
            d.windowScale = Cycle(d.windowScale, scales, delta);
            break;
        case kAspect:
            if (delta % 2) d.stretch = !d.stretch;
            break;
        case kViewport:
            d.viewport = Wrap(d.viewport + delta, 5);
            break;
        case kBlood:
            if (delta % 2) d.redBlood = !d.redBlood;
            break;
        case kCrt:
            d.crt = Wrap(d.crt + delta, 4);
            break;
        case kRender:
            d.renderScale = Wrap(d.renderScale - 1 + delta, 4) + 1;
            break;
        case kInteger:
            if (delta % 2) d.integerScale = !d.integerScale;
            break;
        case kPreset: {
            auto n = PresetName(d);
            int p = n == "Original" ? 0 : n == "Clean" ? 1 : n == "Enhanced" ? 2 : -1;
            ApplyPreset(d, Wrap(p + delta, 3));
            break;
        }
        case kMsaa:
            g.msaa = Cycle(g.msaa, samples, delta);
            break;
        case kPostAA:
            g.postAA = Wrap(g.postAA + delta, 4);
            break;
        case kUpscaler:
            g.spatialUpscaler = Wrap(g.spatialUpscaler + delta, 2);
            break;
        case kAniso:
            g.anisotropy = Cycle(g.anisotropy, af, delta);
            break;
        case kFiltering:
            g.textureFilter = Wrap(g.textureFilter + delta, 3);
            break;
        case kDownsample:
            if (delta % 2) g.downsample = !g.downsample;
            break;
        case kSharpen:
            g.sharpening = Wrap(g.sharpening + delta, 4);
            break;
        case kCoverage:
            if (delta % 2) g.alphaToCoverage = !g.alphaToCoverage;
            break;
        case kPerPixel:
            if (delta % 2) g.perPixelLighting = !g.perPixelLighting;
            break;
        case kSoftParticles:
            g.softParticles = Wrap(g.softParticles + delta, 4);
            break;
        case kMaterialMaps:
            if (delta % 2) g.materialMaps = !g.materialMaps;
            break;
        case kAmbientOcclusion:
            g.ambientOcclusion = Wrap(g.ambientOcclusion + delta, 4);
            break;
        case kHdrOutput:
            if (delta % 2) g.hdrOutput = !g.hdrOutput;
            break;
        case kPaperWhite:
            g.hdrPaperWhite = std::clamp(g.hdrPaperWhite + delta * 20, 80, 400);
            break;
        case kTemporalAA:
            if (delta % 2) g.temporalAA = !g.temporalAA;
            break;
        case kShadowMaps:
            g.shadowMaps = Wrap(g.shadowMaps + delta, 4);
            break;
        case kFog:
            g.volumetricFog = Wrap(g.volumetricFog + delta, 4);
            break;
        case kReflections:
            g.reflections = Wrap(g.reflections + delta, 4);
            break;
        case kCinematicFocus:
            g.cinematicFocus = Wrap(g.cinematicFocus + delta, 4);
            break;
        case kResolutionMode:
            d.resolutionMode = Wrap(d.resolutionMode + delta, 4);
            break;
        case kDynamicResolution:
            if (delta % 2) d.dynamicResolution = !d.dynamicResolution;
            break;
        case kGpuBudget: {
            const int budgets[] = {60, 120, 144};
            d.gpuBudgetFps = Cycle(d.gpuBudgetFps, budgets, delta);
            break;
        }
        case kCrtMask:
            g.crtMask = Wrap(g.crtMask + delta, 4);
            break;
        case kCrtCurvature:
            g.crtCurvature = Wrap(g.crtCurvature + delta, 4);
            break;
        case kCrtGlow:
            g.crtGlow = Wrap(g.crtGlow + delta, 4);
            break;
        case kRenderer:
            d.renderer = Wrap(d.renderer + delta, 3);
            if (d.renderer != 2) {
                d.temporalUpscaler = 0;
                d.frameGeneration = 0;
                d.rayShadows = false;
            }
            break;
        case kTemporalUpscaler:
            d.temporalUpscaler = Wrap(d.temporalUpscaler + delta, 5);
            if (d.temporalUpscaler) d.renderer = 2;
            if (d.temporalUpscaler == 2 && d.temporalQuality == 3) d.temporalQuality = 0;
            break;
        case kTemporalQuality:
            d.temporalQuality = Wrap(d.temporalQuality + delta, d.temporalUpscaler == 2 ? 3 : 4);
            break;
        case kFrameGeneration:
            d.frameGeneration = Wrap(d.frameGeneration + delta, 3);
            if (d.frameGeneration) {
                d.renderer = 2;
            }
            break;
        case kRenderInterpolation:
            if (delta % 2) d.renderInterpolation = !d.renderInterpolation;
            break;
        case kRayShadows:
            if (delta % 2) d.rayShadows = !d.rayShadows;
            if (d.rayShadows) d.renderer = 2;
            break;
        case kIndependentUi:
            if (delta % 2) g.outputResolutionHud = !g.outputResolutionHud;
            break;
        case kCrosshairs:
            if (delta % 2) d.showCrosshairs = !d.showCrosshairs;
            break;
        case kMeshRefinement:
            g.meshRefinement = Wrap(g.meshRefinement + delta, 4);
            break;
        case kBloom:
            g.bloom = Wrap(g.bloom + delta, 4);
            break;
        case kBrightness:
            g.brightness = std::clamp(g.brightness + delta * 5, 0, 100);
            break;
        case kGamma:
            g.gamma = std::clamp(g.gamma + delta * 5, 0, 100);
            break;
        case kContrast:
            g.contrast = std::clamp(g.contrast + delta * 5, 0, 100);
            break;
        case kSaturation:
            g.saturation = std::clamp(g.saturation + delta * 5, 0, 100);
            break;
        case kColorGrade:
            g.colorGrade = Wrap(g.colorGrade + delta, 4);
            break;
        case kVsync:
            g.syncInterval = Wrap(g.syncInterval + delta, 2);
            break;
        case kTearing:
            if (delta % 2) g.allowTearing = !g.allowTearing;
            break;
        case kLowLatency:
            if (delta % 2) g.lowLatency = !g.lowLatency;
            break;
        case kFrameCap:
            d.frameCap = Cycle(d.frameCap, caps, delta);
            break;
        case kStatistics:
            if (delta % 2) d.statistics = !d.statistics;
            break;
        case kMenuStyle:
            if (delta % 2) d.nightShift = !d.nightShift;
            break;
        case kMenuOpacity:
            d.menuOpacity = std::clamp(d.menuOpacity + delta * 5, 25, 100);
            break;
        default:
            break;
    }
}
bool DisplayMenu::Disabled(int row, const DisplaySettings& d) const {
    switch (row) {
        case kDifficulty:
        case kLives:
        case kContinues:
        case kCrosshairP1:
        case kCrosshairP2:
        case kResetGameplay:
            return !optionValues_.gameplayEditable;
        case kScale:
            return d.fullscreen;
        case kMenuOpacity:
            return !nightShiftActive_;
        case kInteger:
            return d.viewport != 0 || d.stretch;
        case kRender:
            return d.resolutionMode != 0;
        case kFrameCap:
            return frameGenerationActive_;
        case kTearing:
            return !caps_.tearing || d.graphics.syncInterval != 0;
        case kLowLatency:
            return !caps_.lowLatency;
        case kHdrOutput:
            return !caps_.hdrOutput;
        case kPaperWhite:
            return !caps_.hdrOutput || !d.graphics.hdrOutput;
        case kTemporalQuality:
            return d.temporalUpscaler == 0;
        case kTemporalAA:
            return !caps_.temporalAA;
        case kMsaa:
            return !caps_.msaa2 && !caps_.msaa4;
        case kAniso:
            return caps_.maxAnisotropy <= 1;
        case kCoverage:
            return d.graphics.msaa <= 1;
        case kUpscaler:
            return !caps_.spatialFSR1;
        case kSoftParticles:
            return !caps_.softParticles;
        case kMaterialMaps:
            return !caps_.materialMaps || activeMaterialMaps_ == 0;
        case kMeshRefinement:
            return activeRefinedModels_ == 0;
        case kPerPixel:
            return caps_.shadowMaps && d.graphics.shadowMaps > 0;
        case kAmbientOcclusion:
            return !caps_.ambientOcclusion;
        case kShadowMaps:
            return !caps_.shadowMaps;
        case kIndependentUi:
            return !caps_.outputResolutionHud;
        case kRayShadows:
            return !raytracingAvailable_ && !d.rayShadows;  // allow clearing an unsupported saved request after startup fallback
        case kFog:
        case kReflections:
        case kCinematicFocus:
            return !caps_.sceneEffects;
        case kRenderInterpolation:
            return frameGenerationActive_ || (d.frameCap != 0 && d.frameCap <= 60);
        case kDynamicResolution:
            return !caps_.gpuTiming || temporalInputActive_;
        case kGpuBudget:
            return !d.dynamicResolution || !caps_.gpuTiming || temporalInputActive_;
        case kCrtCurvature:
            return !curvatureAvailable_ || frameGenerationActive_;
        default:
            return false;
    }
}
std::string DisplayMenu::ShownValue(const DisplaySettings& d, int row) const {
    if (const int volume = VolumeIndex(row); volume >= 0)
        return std::to_string(std::clamp(optionValues_.audio[volume], 0, 10)) + " / 10";
    switch (row) {
        case kDifficulty: {
            const char* names[] = {"Very Easy", "Easy", "Normal", "Hard", "Very Hard"};
            return names[std::clamp(optionValues_.difficulty, 0, 4)];
        }
        case kLives: return std::to_string(optionValues_.lives);
        case kContinues: return optionValues_.credits == 0 ? "Free Play" : std::to_string(optionValues_.credits);
        case kCrosshairP1:
        case kCrosshairP2: return "Style " + std::to_string(optionValues_.crosshair[row == kCrosshairP2] + 1);
        case kLightguns:
        case kWiiRemotes: return "Open";
        case kResetGameplay:
        case kResetGraphics: return "Reset";
        default: break;
    }
    if (row == kPack) return packs_.empty() ? "Original" : packs_[pack_].name.substr(0, 16);
    if (row == kPreset) return PresetName(d, &caps_);
    if (Disabled(row, d)) {
        switch (row) {
            case kMenuOpacity:
                return "Night Shift only";
            case kScale:
                return "Fullscreen";
            case kRender:
                return "Automatic";
            case kInteger:
                return "Needs 4:3 Fit";
            case kFrameCap:
                return "60 FPS (FG)";
            case kTearing:
                return caps_.tearing ? "VSync On" : "Unsupported";
            case kPaperWhite:
                return caps_.hdrOutput ? "HDR Off" : "Unsupported";
            case kCoverage:
                return "Needs MSAA";
            case kTemporalQuality:
                return "Upscaling Off";
            case kGpuBudget:
                return d.dynamicResolution ? "Inactive" : "Dynamic Res Off";
            case kMaterialMaps:
                return activeMaterialMaps_ == 0 ? "Needs pack" : "Unsupported";
            case kMeshRefinement:
                return "Needs pack";
            case kPerPixel:
                return "Auto (shadows)";
            case kRenderInterpolation:
                return frameGenerationActive_ ? "FG Active" : "Above 60 FPS";
            case kCrtCurvature:
                return frameGenerationActive_ ? "FG Active" : "Raw gun input";
            case kDynamicResolution:
                return temporalInputActive_ ? "Stable input" : "Unsupported";
            default:
                return "Unsupported";
        }
    }
    return ValueText(d, row);
}
std::string DisplayMenu::RowHelp(const DisplaySettings& d, int row) const {
    if (Disabled(row, d)) {
        switch (row) {
            case kDifficulty:
            case kLives:
            case kContinues:
            case kCrosshairP1:
            case kCrosshairP2:
            case kResetGameplay:
                return "Saved gameplay settings are locked during a run or network room. Return to the main menu to change them.";
            case kMenuOpacity:
                return "Menu Opacity controls Night Shift panels. Choose Night Shift in Menu Style and restart to use it.";
            case kScale:
                return "Window Scale is available in Windowed mode. Fullscreen modes use the desktop resolution.";
            case kRender:
                return std::to_string(renderWidth_) + " x " + std::to_string(renderHeight_) +
                       " internal pixels. Resolution Mode controls size automatically; choose Fixed Scale to edit Render Scale.";
            case kInteger:
                return "Integer Scaling requires Original 4:3 with Image Fit set to Fit. Your saved preference is retained.";
            case kFrameCap:
                return "Active frame generation uses 60 rendered frames/second. Your normal Frame Limit is retained.";
            case kTearing:
                if (caps_.tearing)
                    return "Turn VSync Off to allow tearing. The saved choice is retained. Monitor VRR must be enabled separately.";
                return "The active renderer cannot offer tearing control. Try Direct3D 11 or native Direct3D 12 on supported Windows "
                       "hardware.";
            case kPaperWhite:
                return "Enable HDR Output first on a supported HDR display. Your saved SDR-white brightness is retained.";
            case kCoverage:
                return "Enable Edge AA (MSAA) 2x or 4x first. Alpha Coverage applies to opaque cutouts, not smoke or ordinary transparency.";
            case kTemporalQuality:
                return "Select an Upscaling provider in Graphics first. Its quality applies after restart; your saved choice is retained.";
            case kGpuBudget:
                if (!d.dynamicResolution)
                    return "Enable dynamic resolution to edit its FPS target. This changes render size, separate from Frame Limit.";
                return "Dynamic resolution is inactive while GPU timing is unavailable or a temporal/ray mode requires stable input "
                       "pixels.";
            case kMaterialMaps:
                if (!caps_.materialMaps)
                    return "Material Detail needs a renderer with material-map support and compatible maps in the loaded pack.";
                return "The loaded pack has no material maps. Choose a compatible pack and restart. Saved preferences are retained.";
            case kMeshRefinement:
                return "The loaded pack lists no models for smoothing. Choose a compatible pack and restart. Saved preferences are "
                       "retained.";
            case kPerPixel:
                return "Raster Shadows uses pixel lighting automatically. Turn Raster Shadows Off to edit the independent lighting "
                       "preference.";
            case kRenderInterpolation:
                if (frameGenerationActive_)
                    return "Pose interpolation is inactive during active frame generation. Your saved preference is retained.";
                return "Pose interpolation needs a Frame Limit above 60 FPS or Unlimited. The simulation still runs at 60 Hz.";
            case kCrtCurvature:
                return "Curvature is inactive for raw gun input or active frame generation, which require a flat image.";
            case kDynamicResolution:
                if (temporalInputActive_)
                    return "The active temporal upscaler or ray-shadow mode requires stable render dimensions. Saved dynamic resolution is "
                           "retained.";
                return "Dynamic resolution requires GPU timestamp support from the active renderer. The saved preference is retained.";
            case kRayShadows:
                return "Ray Shadows needs native Direct3D 12, a DXR 1.1 GPU and the installed shader compiler. Restart with that renderer "
                       "first.";
            default:
                return "The active renderer does not support this setting. Check the selected renderer in Graphics.";
        }
    }
    if (row == kPreset)
        return "Image clarity + Downsample only. Clean: supported MSAA/texture filtering. Enhanced adds " +
               std::string(caps_.smaa ? "SMAA" : "FXAA") + ", Low sharpening" + (caps_.outputResolutionHud ? " and Sharp HUD." : ".");
    if (row == kRender) return std::to_string(renderWidth_) + " x " + std::to_string(renderHeight_) + " internal pixels. " + Help(row);
    if (row == kMonitor && d.monitor > 0 && d.monitor <= static_cast<int>(monitors_.size())) return monitors_[d.monitor - 1];
    if (row == kPack && !packs_.empty()) return packs_[pack_].detail.empty() ? Help(row) : packs_[pack_].detail;
    if (row == kMaterialMaps) return std::to_string(activeMaterialMaps_) + " loaded material maps. " + Help(row);
    if (row == kMeshRefinement) return std::to_string(activeRefinedModels_) + " selected models in the loaded pack. " + Help(row);
    if (row == kContinues && !optionValues_.freePlayUnlocked)
        return "Continue credits for your next local run. Free Play unlocks through the original game progression.";
    return Help(row);
}
bool DisplayMenu::Update(const MenuInput& in, DisplaySettings& d, std::vector<uint32_t>& sounds) {
    if (!open_) return false;
    if (resetConfirm_) {
        // Keep the decision isolated from tabs, setting adjustments and Hide UI.
        // Start cancels this modal; the caller must not treat it as Resume/Back.
        if (in.start || in.reload || (in.pressed & 2)) {
            resetConfirm_ = false;
            pointer_ = {};
            sounds.push_back(game::kOptionBackSound);
            return false;
        }
        const int under = in.pointer ? ResetChoiceAt(in.px, in.py) : -1;
        if (pointer_.Update(in.pointer, in.px, in.py, in.trigger) && under >= 0 && resetYes_ != (under == 1)) {
            resetYes_ = under == 1;
        }
        if (in.pressed & 0xF0) {
            resetYes_ = !resetYes_;
        }
        if ((in.pressed & 0xC) || (in.trigger && under == (resetYes_ ? 1 : 0))) {
            if (resetYes_) {
                if (resetGameplayConfirm_) {
                    if (optionValues_.gameplayEditable) gameplayReset_ = true;
                } else {
                    const bool nightShift = d.nightShift, blood = d.redBlood, crosshairs = d.showCrosshairs;
                    d = DisplaySettings{};
                    d.nightShift = nightShift;
                    d.redBlood = blood;
                    d.showCrosshairs = crosshairs;
                    graphicsReset_ = true;  // acknowledge and persist even if already at defaults
                    status_ = "Graphics defaults restored. Restart may be required.";
                }
            }
            resetConfirm_ = false;
            pointer_ = {};
            sounds.push_back(resetYes_ ? game::kOptionSelectSound : game::kOptionBackSound);
        }
        return false;
    }
    const bool previewClick = in.pointer && in.trigger && PreviewButtonAt(in.px, in.py);
    if (uiHidden_) {
        // Track motion without moving selection while the page is hidden. A
        // stationary pointer must not steal keyboard focus after restoration.
        pointer_.Update(in.pointer, in.px, in.py, false);
        if (in.preview || previewClick || in.start || in.reload || (in.pressed & 0xEu)) {
            uiHidden_ = false;
            sounds.push_back(in.start || in.reload || (in.pressed & 2) ? game::kOptionBackSound : game::kOptionSelectSound);
        }
        return false;  // inspecting the scene never edits a hidden row or resumes the game
    }
    if (calibration_ < 0 && (in.preview || previewClick)) {
        uiHidden_ = true;
        pointer_.Update(in.pointer, in.px, in.py, false);
        sounds.push_back(game::kOptionSelectSound);
        return false;
    }
    if (calibration_ >= 0) {
        if (in.start || (in.pressed & 2) || in.reload) {
            calibration_ = -1;
            sounds.push_back(game::kOptionBackSound);
            return false;
        }
        const int direction = ((in.pressed & 0x80) ? 1 : 0) - ((in.pressed & 0x40) ? 1 : 0);
        bool interacted = direction || in.trigger || (in.pressed & 0xc);
        if (interacted) calibration_ = Wrap(calibration_ + (direction ? direction : 1), 4);
        const int adjust = ((in.pressed & 0x10) ? 1 : 0) - ((in.pressed & 0x20) ? 1 : 0);
        const int adjustedRow = calibration_ == 0   ? kBrightness
                                : calibration_ == 1 ? kContrast
                                : calibration_ == 2 ? kGamma
                                                    : kCrtCurvature;
        if (adjust && !Disabled(adjustedRow, d)) {
            const auto before = d;
            Step(d, adjustedRow, adjust);
            interacted = interacted || d != before;
        }
        if (interacted) sounds.push_back(game::kOptionSelectSound);
        return false;
    }
    if (in.start || (in.pressed & 2)) {
        Close();
        sounds.push_back(game::kOptionBackSound);
        return true;
    }
    int tab = in.pointer ? TabAt(in.px, in.py) : -1;
    bool moved = pointer_.Update(in.pointer, in.px, in.py, in.trigger || in.reload);
    if (tab >= 0 && in.trigger) {
        SetPage(static_cast<Page>(tab));
        return false;
    }
    if (in.pageDelta) {
        SetPage(static_cast<Page>(Wrap(int(page_) + in.pageDelta, kPageCount)));
        return false;
    }
    // Separate pointer targets keep wheel-free gun/controller navigation available on long pages.
    const int scrollButton = in.pointer && in.trigger && in.px >= 616 && in.px < 640
                                 ? in.py >= kContentTop && in.py < kContentTop + kScrollButtonHeight ? 1
                                   : in.py >= kContentBottom - kScrollButtonHeight && in.py < kContentBottom ? -1 : 0
                                 : 0;
    const int scroll = in.scroll || scrollButton ? in.scroll + scrollButton : 0;
    if (scroll) {
        scroll_ = std::clamp(scroll_ - scroll * kRowHeight * 3, 0, MaxScroll(page_));
        auto visible = VisibleRows();
        if (std::find(visible.begin(), visible.end(), cursor_) == visible.end() && visible.size() > 1)
            cursor_ = scroll > 0 ? visible.front() : visible[visible.size() - 2];
        return false;
    }
    const auto& rows = Rows(page_);
    int under = in.pointer ? RowAt(in.px, in.py, page_, scroll_) : -1;
    if (moved && under >= 0 && under != cursor_) {
        cursor_ = under;
    }
    int index = static_cast<int>(std::find(rows.begin(), rows.end(), cursor_) - rows.begin());
    if (in.pressed & 0x10) {
        cursor_ = rows[Wrap(index - 1, static_cast<int>(rows.size()))];
    }
    if (in.pressed & 0x20) {
        cursor_ = rows[Wrap(index + 1, static_cast<int>(rows.size()))];
    }
    KeepCursorVisible();
    if (cursor_ == kResetGraphics || cursor_ == kResetGameplay) {
        if (in.reload) {
            Close();
            sounds.push_back(game::kOptionBackSound);
            return true;
        }
        if (!Disabled(cursor_, d) && ((in.pressed & 0xC) || (under == cursor_ && in.trigger))) {
            resetConfirm_ = true;
            resetYes_ = false;
            resetGameplayConfirm_ = cursor_ == kResetGameplay;
            sounds.push_back(game::kOptionSelectSound);
        }
        return false;  // an action has no value to cycle with Left / Right
    }
    bool value = cursor_ != kBack, over = under == cursor_;
    if (in.reload && !(value && over)) {
        Close();
        sounds.push_back(game::kOptionBackSound);
        return true;
    }
    int delta = ((in.pressed & 0x80) ? 1 : 0) - ((in.pressed & 0x40) ? 1 : 0);
    if (cursor_ == kLightguns || cursor_ == kWiiRemotes || cursor_ == kCalibration) {
        if ((in.pressed & 0xC) || (in.trigger && over)) {
            if (cursor_ == kCalibration) calibration_ = 0;
            else deviceAction_ = cursor_ == kLightguns ? DeviceAction::Lightguns : DeviceAction::WiiRemotes;
            sounds.push_back(game::kOptionSelectSound);
        }
        return false;
    }
    if (value) {
        if (in.pressed & 0xC) ++delta;
        if (over && in.trigger) delta += game::MenuPointerStep(in.px, 360);
        if (over && in.reload) --delta;
    } else if (delta) {
        SetPage(static_cast<Page>(Wrap(page_ + delta, kPageCount)));
        return false;
    }
    const bool disabled = Disabled(cursor_, d);
    if (value && delta && !disabled) {
        auto before = d;
        const int volume = VolumeIndex(cursor_);
        const bool gameplayValue = cursor_ >= kDifficulty && cursor_ <= kCrosshairP2;
        if (volume >= 0 || gameplayValue) {
            const auto oldValues = optionValues_;
            if (volume >= 0) optionValues_.audio[volume] = std::clamp(optionValues_.audio[volume] + delta, 0, 10);
            else if (cursor_ == kDifficulty) optionValues_.difficulty = Wrap(optionValues_.difficulty + delta, 5);
            else if (cursor_ == kLives) optionValues_.lives = Wrap(optionValues_.lives - 1 + delta, 5) + 1;
            else if (cursor_ == kContinues)
                optionValues_.credits = optionValues_.freePlayUnlocked ? Wrap(optionValues_.credits + delta, 10)
                                                                      : Wrap(optionValues_.credits - 1 + delta, 9) + 1;
            else {
                int& sight = optionValues_.crosshair[cursor_ == kCrosshairP2];
                sight = Wrap(sight + delta, 4);
            }
            if (optionValues_ != oldValues) {
                optionChanged_ = true;
                sounds.push_back(game::kOptionSelectSound);
            }
            return false;
        }
        if (cursor_ == kCalibration) {
            calibration_ = 0;
            sounds.push_back(game::kOptionSelectSound);
            return false;
        }
        if (cursor_ == kMonitor)
            d.monitor = Wrap(d.monitor + delta, static_cast<int>(monitors_.size()) + 1);
        else if (cursor_ == kPack && !packs_.empty()) {
            const int direction = delta > 0 ? 1 : -1;
            int next = pack_;
            for (size_t i = 0; i < packs_.size(); ++i) {
                next = Wrap(next + direction, static_cast<int>(packs_.size()));
                if (packs_[next].valid) break;
            }
            if (packs_[next].valid) {
                pack_ = next;
                packChanged_ = true;
                status_ = "Content pack applies on next launch. Loaded pack is unchanged.";
            } else
                status_ = packs_[next].detail;
        } else {
            if (cursor_ == kPreset) {
                const auto name = PresetName(d, &caps_);
                const int preset = name == "Original" ? 0 : name == "Clean" ? 1 : name == "Enhanced" ? 2 : -1;
                ApplyPreset(d, Wrap(preset + delta, 3), &caps_);
            } else {
                Step(d, cursor_, delta);
            }
            const int direction = delta > 0 ? 1 : -1;
            if (cursor_ == kPostAA)
                for (int i = 0; i < 4 && ((d.graphics.postAA == 3 && !caps_.cmaa2) || (d.graphics.postAA == 2 && !caps_.smaa)); ++i)
                    Step(d, cursor_, direction);
            if (cursor_ == kMsaa)
                for (int i = 0; i < 3 && ((d.graphics.msaa == 4 && !caps_.msaa4) || (d.graphics.msaa == 2 && !caps_.msaa2)); ++i)
                    Step(d, cursor_, direction);
            if (cursor_ == kAniso)
                for (int i = 0; i < 5 && d.graphics.anisotropy > caps_.maxAnisotropy; ++i) Step(d, cursor_, direction);
            if (cursor_ == kRenderer || cursor_ == kTemporalUpscaler || cursor_ == kTemporalQuality || cursor_ == kFrameGeneration)
                status_ = "Saved choice applies on restart. SDK and GPU support are checked at launch.";
            if (cursor_ == kRenderer && d.renderer != 2)
                status_ = "Restart required. Vendor upscaling, frame generation and ray requests cleared.";
            if (cursor_ == kMenuStyle) status_ = "Menu style applies on next launch.";
        }
        if (d != before || cursor_ == kPack) sounds.push_back(game::kOptionSelectSound);
    }
    if (!value && ((in.pressed & 0xC) || (in.trigger && over))) {
        sounds.push_back(game::kOptionBackSound);
        Close();
        return true;
    }
    return false;
}
void DisplayMenu::Sprites(const game::ExeImage& exe, const DisplaySettings& d, std::vector<game::SpriteDef>& out, bool overScene) const {
    if (!open_) return;
    const size_t first = out.size();
    auto finish = [&] {
        if (overScene)
            for (size_t i = first; i < out.size(); ++i) {
                out[i].depth = .0002f;
                out[i].flags |= 0x700;
            }
    };
    auto text = [&](float x, float y, const std::string& value, float scale, uint32_t flags = kNormal) {
        game::MenuTextScaled(exe, x, y, value, flags, scale, out);
    };
    if (uiHidden_) {
        text(kPreviewTextX, kPreviewTextY, "Show UI [H]", kPreviewTextScale);
        finish();
        return;
    }
    if (!overScene) game::MenuBackground(out);
    if (resetConfirm_) {
        const bool gameplay = resetGameplayConfirm_;
        text(112, 128, gameplay ? "RESET GAMEPLAY?" : "RESET ALL GRAPHICS?", .9f);
        text(112, 181, gameplay ? "Restore new-game rules and sight defaults?" : "Restore all display and graphics defaults?", .5f);
        text(112, 208, gameplay ? "Difficulty, lives, credits, sight styles," : "Renderer, upscaling, frame generation,", .5f);
        text(112, 225, gameplay ? "blood color and gameplay sight visibility." : "ray shadows and menu opacity.", .5f);
        text(112, 250, gameplay ? "Keeps records, unlocks, audio and calibration." : "Keeps gameplay, sound, packs and menu theme.", .5f);
        text(kResetLeft + 40, kResetTop + 9, "[ RESET ]", .6f, resetYes_ ? kSelected : kNormal);
        text(kCancelLeft + 36, kResetTop + 9, "[ CANCEL ]", .6f, resetYes_ ? kNormal : kSelected);
        if (!gameplay) text(112, 341, "A restart may be needed for renderer changes.", .4f);
        text(112, 365, "RELOAD / BACK / START: CANCEL", .4f);
        finish();
        return;
    }
    if (calibration_ >= 0) {
        const char* names[] = {"BLACK LEVEL", "WHITE LEVEL", "GAMMA REFERENCE", "GEOMETRY"};
        const int adjusted = calibration_ == 0 ? kBrightness : calibration_ == 1 ? kContrast : calibration_ == 2 ? kGamma : kCrtCurvature;
        text(20, 425, std::string(names[calibration_]) + "  /  " + ShownValue(d, adjusted), .6f);
        text(20, 453, "LEFT/RIGHT: PATTERN   UP/DOWN: ADJUST   RELOAD / BACK: RETURN", .4f);
        finish();
        return;
    }
    text(26, 20, "OPTIONS", 1.25f);
    for (int page = 0; page < kPageCount; ++page)
        text(TabLeft(page) + 4, 80, kPages[page], .55f, page == page_ ? kSelected : kNormal);
    const auto visible = VisibleRows();
    if (scroll_ > 0 && visible.size() > 1) text(34, 111, SectionAt(page_, visible.front()), .35f, 0x40);
    for (int row : visible) {
        const float top = RowY(row, page_, scroll_) * 24 + 10;
        const bool disabled = Disabled(row, d);
        const uint32_t flags = (row == cursor_ ? kSelected : kNormal) | (disabled ? 8 : 0);
        if (row != kBack && *GroupStart(page_, row) && top - kGroupHeight >= kContentTop)
            text(34, top - kGroupHeight - 5, GroupStart(page_, row), .35f, 0x40);
        text(34, top - 5, Label(row), .6f, flags);
        const auto value = ShownValue(d, row);
        if (!value.empty()) text(402, top - 5, value, std::min(.6f, 180.f / (float(value.size()) * 16.f)), flags);
        if (row == cursor_ && row != kBack && !IsAction(row) && !disabled)
            game::MenuAdjustArrows(360, 600, top + 12, out);
    }
    if (MaxScroll(page_) > 0) {
        text(621, kContentTop + 3, "^", .5f, scroll_ > 0 ? kNormal : 0x40);
        text(621, kContentBottom - 25, "v", .5f, scroll_ < MaxScroll(page_) ? kNormal : 0x40);
        const auto& rows = Rows(page_);
        if (visible.size() > 1) {
            const int begin = int(std::find(rows.begin(), rows.end(), visible.front()) - rows.begin()) + 1;
            text(470, kBackTop + 2, std::to_string(begin) + "-" + std::to_string(begin + visible.size() - 2) + " / " + std::to_string(rows.size() - 1), .35f, 0x40);
        }
    }
    if (page_ == kPacks) text(34, 219, "Loaded: " + activePackName_.substr(0, 58), .45f, 0x40);
    text(kPreviewTextX, kPreviewTextY, "Hide UI [H]", kPreviewTextScale);
    finish();
}
void DisplayMenu::ModernHud(const DisplaySettings& d, game::WorldHud& hud) const {
    hud.nightShiftPage = 5;
    hud.menuPanels = {{0, 0, 640, 480, 0xEC090D10}, {26, 65, 614, 67, 0xFF92272D}};
    hud.menuText.clear();
    auto text = [&](float x, float y, const std::string& value, float scale, uint32_t color = 0xFFECE7DC) {
        hud.menuText.push_back({x, y, value, scale, color});
    };
    if (resetConfirm_) {
        const bool gameplay = resetGameplayConfirm_;
        hud.menuPanels = {{88, 106, 552, 390, 0xF4090D10}, {112, 164, 528, 166, 0xFF92272D}};
        text(112, 128, gameplay ? "RESET GAMEPLAY?" : "RESET ALL GRAPHICS?", .9f);
        text(112, 181, gameplay ? "Restore new-game rules and sight defaults?" : "Restore all display and graphics defaults?", .5f);
        text(112, 208, gameplay ? "Difficulty, lives, credits, sight styles," : "Renderer, upscaling, frame generation,", .5f, 0xFFC1BAAC);
        text(112, 225, gameplay ? "blood color and gameplay sight visibility." : "ray shadows and menu opacity.", .5f, 0xFFC1BAAC);
        text(112, 250, gameplay ? "Keeps records, unlocks, audio and calibration." : "Keeps gameplay, sound, packs and menu theme.", .5f, 0xFFC1BAAC);
        if (!gameplay) text(112, 341, "A restart may be needed for renderer changes.", .4f, 0xFFC1BAAC);
        text(112, 365, "RELOAD / BACK / START: CANCEL", .4f, 0xFFC1BAAC);
        for (int choice = 0; choice < 2; ++choice) {
            const bool reset = choice == 1;
            const float left = reset ? kResetLeft : kCancelLeft, right = reset ? kResetRight : kCancelRight;
            hud.menuPanels.push_back({left, kResetTop, right, kResetBottom, reset == resetYes_ ? 0xDE8F2026u : 0xB82B3033u});
            text(left + 22, kResetTop + 9, reset ? "RESET" : "CANCEL", .6f);
        }
        return;
    }
    if (uiHidden_) {
        hud.menuPanels = {{550, 456, 630, 476, 0xE6090D10}};
        text(kPreviewTextX, kPreviewTextY, "Show UI [H]", kPreviewTextScale);
        return;
    }
    if (calibration_ >= 0) {
        const char* names[] = {"BLACK LEVEL", "WHITE LEVEL", "GAMMA REFERENCE", "GEOMETRY"};
        hud.menuPanels = {{0, 416, 640, 480, 0xE6090D10}};
        const int adjusted = calibration_ == 0 ? kBrightness : calibration_ == 1 ? kContrast : calibration_ == 2 ? kGamma : kCrtCurvature;
        text(20, 425, std::string(names[calibration_]) + "  /  " + ShownValue(d, adjusted), .6f);
        text(20, 453, "LEFT/RIGHT: PATTERN   UP/DOWN: ADJUST   RELOAD / BACK: RETURN", .4f);
        return;
    }
    text(26, 20, "OPTIONS", 1.25f);
    for (int page = 0; page < kPageCount; ++page) {
        if (page == page_) hud.menuPanels.push_back({TabLeft(page), 78, TabLeft(page) + TabWidth(page) - 3, 111, 0xFF8F2026});
        text(TabLeft(page) + 4, 85, kPages[page], .55f);
    }
    const auto visible = VisibleRows();
    if (scroll_ > 0 && visible.size() > 1) text(34, 115, SectionAt(page_, visible.front()), .34f, 0xFFC1BAAC);
    for (int row : visible) {
        const float top = RowY(row, page_, scroll_) * 24 + 10;
        const bool disabled = Disabled(row, d);
        if (row != kBack && *GroupStart(page_, row) && top - kGroupHeight >= kContentTop)
            text(34, top - kGroupHeight, GroupStart(page_, row), .34f, 0xFFC1BAAC);
        if (row == cursor_) hud.menuPanels.push_back({26, top - 2, 614, top + 23, 0xCC8F2026});
        else hud.menuPanels.push_back({26, top + 23, 614, top + 24, 0x354E514E});
        const uint32_t color = disabled ? 0xFF797D79 : 0xFFECE7DC;
        text(34, top, Label(row), .6f, color);
        const auto value = ShownValue(d, row);
        text(402, top, value, std::min(.6f, value.empty() ? .6f : 180.f / (float(value.size()) * 16.f)), color);
        if (row == cursor_ && row != kBack && !IsAction(row) && !disabled) {
            text(358, top, "<", .6f);
            text(600, top, ">", .6f);
        }
    }
    if (MaxScroll(page_) > 0) {
        const float thumbTop = kContentTop + kScrollButtonHeight + 2;
        const float track = kContentBottom - kScrollButtonHeight - 10 - thumbTop;
        const float thumb = thumbTop + track * float(scroll_) / float(MaxScroll(page_));
        hud.menuPanels.push_back({625, kContentTop, 638, kContentTop + kScrollButtonHeight, scroll_ > 0 ? 0xAB333A3C : 0x50333A3Cu});
        hud.menuPanels.push_back({625, kContentBottom - kScrollButtonHeight, 638, kContentBottom, scroll_ < MaxScroll(page_) ? 0xAB333A3C : 0x50333A3Cu});
        hud.menuPanels.push_back({629, thumb, 634, thumb + 8, 0xFFC1BAAC});
        text(627, kContentTop + 10, "^", .5f, scroll_ > 0 ? 0xFFECE7DC : 0xFF797D79);
        text(627, kContentBottom - kScrollButtonHeight + 10, "v", .5f, scroll_ < MaxScroll(page_) ? 0xFFECE7DC : 0xFF797D79);
        const auto& rows = Rows(page_);
        if (visible.size() > 1) {
            const int begin = int(std::find(rows.begin(), rows.end(), visible.front()) - rows.begin()) + 1;
            text(470, kBackTop + 8, std::to_string(begin) + "-" + std::to_string(begin + visible.size() - 2) + " / " + std::to_string(rows.size() - 1), .35f, 0xFFC1BAAC);
        }
    }
    if (page_ == kPacks) text(34, 225, "Loaded: " + activePackName_.substr(0, 58), .45f, 0xFFC1BAAC);
    hud.menuPanels.push_back({550, 456, 630, 476, 0xE6090D10});
    text(kPreviewTextX, kPreviewTextY, "Hide UI [H]", kPreviewTextScale);
}
}  // namespace play

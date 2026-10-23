#pragma once

// play.exe's display settings page (not in the original), drawn in the options screen's look (game/menu_text.h) and
// shown from two places: the pause menu's "Display" row (pause_menu.h) and the main menu's Options screen, whose
// port-only "Display Settings" row (world_modes_options.cpp row 14) asks play.exe to show this page over it
// (WorldHud::portDisplayRequests). Pure state machine (tests/pause_menu_test.cpp); the caller applies the settings
// (notes/agents/display.md): window / fullscreen through the platform layer, the present mapping and the internal
// resolution through the GPU device.
//   A single options screen groups settings into Display, Graphics, Gameplay, Sound, Devices, CRT and Packs.
//   Long categories scroll within a fixed viewport; tabs, Back and Hide UI remain available. Saved settings retain
//   their existing keys and the caller applies gameplay edits through the world's persistent settings API.
//   Keyboard: Up / Down move, Left / Right change a value, Enter / Space on a value = next value, on Back = close,
//   Backspace = close. Pointer: aiming at a row highlights it; trigger = next value / Back, reload = previous value /
//   close. Start closes a standalone page; the pause-menu caller uses it to resume instead.

#include <cstdint>
#include <string>
#include <vector>

#include "game/exe_image.h"
#include "game/menu_pointer.h"
#include "game/menu_scroll.h"
#include "game/sprite.h"
#include "game/world.h"
#include "port/render/graphics_options.h"

namespace play {

struct DisplaySettings {
    bool fullscreen = false;
    bool exclusiveFullscreen = false;
    int windowScale = 2;    // 1..3: that multiple of 640x480 as the windowed client size; 0: fit the usable desktop
    bool stretch = false;   // false: the 4:3 image centred with bars; true: stretched to the window
    int viewport = 0;       // 0 original, 1 16:9, 2 21:9, 3 32:9, 4 current window. Wider framing is experimental.
    bool redBlood = false;  // local cosmetic choice: false = green, true = red
    int crt = 0;            // 0 off, 1..3 scanline strength
    int renderScale = 2;    // internal resolution of the frame: 1..4 x 640x480 (1 = the original; default 2 = the 2x window's
                            // own pixels, at least as sharp as the earlier 960x720 default)
    port::GraphicsOptions graphics;
    bool integerScale = false;
    int frameCap = 60;  // presentation only: 0,30,60,120,144,165,240. Simulation remains 60 Hz.
    int monitor = 0;    // 0 current display, otherwise one-based enumerated monitor; missing displays fall back safely
    bool statistics = false;
    bool nightShift = true;
    int menuOpacity = 65;  // 25..100 percent strength of menu backgrounds; text remains fully readable
    bool showCrosshairs = true;  // local gameplay sights for both players; menu/calibration pointers stay visible
    int resolutionMode = 0;  // authored scale, match output, SSAA150%, SSAA200%
    bool dynamicResolution = false;
    int gpuBudgetFps = 60;
    int renderer = 0;                  // D3D11, SDL GPU, native D3D12; restart required
    int temporalUpscaler = 0;          // Off, DLSS, FSR2, FSR3, XeSS; restart required
    int temporalQuality = 0;           // Quality, Balanced, Performance, native AA
    int frameGeneration = 0;           // optional DLSS FG, native D3D12; restart required
    bool renderInterpolation = false;  // render-only intermediate poses, up to one simulation tick of visual delay
    bool rayShadows = false;
    bool operator==(const DisplaySettings&) const = default;
    bool operator!=(const DisplaySettings& o) const { return !(*this == o); }
};

// Input of one frame for the port's menus (the pause menu and this page).
struct MenuInput {
    uint32_t pressed = 0;  // pad bits that went down this frame: 0x10 up, 0x20 down, 0x40 left, 0x80 right, 0x8 enter,
                           // 0x4 confirm, 0x2 back
    bool pointer = false;  // a pointing device aims at (px, py), 640 x 480 game pixels
    float px = 0, py = 0;
    bool trigger = false;  // the device's trigger went down this frame
    bool reload = false;   // the reload button went down
    bool start = false;    // the start button went down (the original's resume)
    bool preview = false;  // local H shortcut: hide/show the graphics UI without resuming the game
    int scroll = 0;        // mouse-wheel notches: positive scrolls toward the first row
    int pageDelta = 0;     // previous / next category (Page Up / Page Down)
};

class DisplayMenu {
   public:
    enum Page {
        kDisplay = 0, kGraphics, kGameplay, kSound, kDevices, kRetro, kPacks, kPageCount,
        // Source aliases for callers that previously selected one of the separate graphics pages.
        kClarity = kGraphics, kLighting = kGraphics, kPerformance = kDisplay, kColor = kDisplay,
        kResolution = kGraphics, kTemporal = kGraphics
    };
    enum Row {
        kFullscreen = 0,
        kScale,
        kAspect,
        kCrt,
        kRender,
        kBlood,
        kBack,
        kInteger,
        kPreset,
        kMsaa,
        kPostAA,
        kAniso,
        kFiltering,
        kDownsample,
        kSharpen,
        kCoverage,
        kPerPixel,
        kBloom,
        kBrightness,
        kGamma,
        kContrast,
        kSaturation,
        kColorGrade,
        kVsync,
        kFrameCap,
        kMonitor,
        kStatistics,
        kMenuStyle,
        kPack,
        kTearing,
        kLowLatency,
        kUpscaler,
        kViewport,
        kSoftParticles,
        kMaterialMaps,
        kAmbientOcclusion,
        kHdrOutput,
        kPaperWhite,
        kTemporalAA,
        kShadowMaps,
        kFog,
        kReflections,
        kCinematicFocus,
        kResolutionMode,
        kDynamicResolution,
        kGpuBudget,
        kCrtMask,
        kCrtCurvature,
        kCrtGlow,
        kCalibration,
        kRenderer,
        kTemporalUpscaler,
        kTemporalQuality,
        kFrameGeneration,
        kMeshRefinement,
        kRenderInterpolation,
        kIndependentUi,
        kRayShadows,
        kMenuOpacity,
        kResetGraphics,
        kCrosshairs,
        kDifficulty,
        kLives,
        kContinues,
        kCrosshairP1,
        kCrosshairP2,
        kResetGameplay,
        kMasterVolume,
        kMusicVolume,
        kGunVolume,
        kEffectsVolume,
        kAttractionVolume,
        kLightguns,
        kWiiRemotes,
        kRowCount
    };

    // Cached values are supplied by the caller. Audio is local; gameplay values use the world's save file.
    struct OptionValues {
        int audio[5] = {5, 5, 5, 5, 5};  // music, guns, effects, master, attraction
        int difficulty = 2, lives = 3, credits = 5, crosshair[2] = {};
        bool freePlayUnlocked = false, gameplayEditable = true;
        bool operator==(const OptionValues&) const = default;
    };
    enum class DeviceAction { None, Lightguns, WiiRemotes };
    void SetOptionValues(const OptionValues& values) { optionValues_ = values; }
    const OptionValues& OptionValuesState() const { return optionValues_; }
    bool TakeOptionChange(OptionValues* values) {
        if (!optionChanged_ || !values) return false;
        *values = optionValues_;
        optionChanged_ = false;
        return true;
    }
    bool TakeGameplayReset() {
        const bool reset = gameplayReset_;
        gameplayReset_ = false;
        return reset;
    }
    DeviceAction TakeDeviceAction() {
        const auto action = deviceAction_;
        deviceAction_ = DeviceAction::None;
        return action;
    }

    struct PackChoice {
        std::string name, path, detail;
        bool valid = true;
    };

    void Open();
    void Close() {
        open_ = false;
        uiHidden_ = false;
        resetConfirm_ = false;
    }
    bool IsOpen() const { return open_; }
    bool UiHidden() const { return uiHidden_; }
    bool ResetConfirming() const { return resetConfirm_; }
    bool ResetSelected() const { return resetYes_; }
    bool TakeGraphicsReset() {
        const bool reset = graphicsReset_;
        graphicsReset_ = false;
        return reset;
    }

    // One frame of the open page: edits d in place. Navigation is silent; interactions and backing out use the shared
    // option sounds (Preview 74 and 76 respectively). Returns true when the page was closed (Back / Start / back button).
    bool Update(const MenuInput& in, DisplaySettings& d, std::vector<uint32_t>& sounds);
    // The page's sprites: the menu background, the heading, the rows with their values (red = selected).
    // overScene: over the paused game instead of the tiled background (play.exe: the settings show live): no background
    // tiles, and the sprites sit at depth 0.0002 with depth func "always", in front of the pause overlay 0x93E (0.0005).
    void Sprites(const game::ExeImage& exe, const DisplaySettings& d, std::vector<game::SpriteDef>& out, bool overScene = false) const;
    void ModernHud(const DisplaySettings& d, game::WorldHud& hud) const;

    int Cursor() const { return cursor_; }
    Page CurrentPage() const { return page_; }
    void SetPage(Page page);
    void FocusRow(int row);  // keyboard/test entry: scroll the real row into view
    int ScrollOffset() const { return scroll_; }
    game::MenuScrollHint ScrollHint() const;
    std::vector<int> VisibleRows() const;
    void SetMonitors(std::vector<std::string> names) { monitors_ = std::move(names); }
    void SetPresentationCapabilities(bool tearing, bool lowLatency, bool hdr = false, bool taa = false, bool cmaa = false) {
        caps_.tearing = tearing;
        caps_.lowLatency = lowLatency;
        caps_.hdrOutput = hdr;
        caps_.temporalAA = taa;
        caps_.cmaa2 = cmaa;
    }
    void SetGraphicsCapabilities(const port::GraphicsCapabilities& caps) { caps_ = caps; }
    void SetNightShiftActive(bool active) { nightShiftActive_ = active; }
    void SetCurvatureAvailable(bool available) { curvatureAvailable_ = available; }
    void SetTemporalInputActive(bool active) { temporalInputActive_ = active; }
    void SetRaytracingAvailable(bool available) { raytracingAvailable_ = available; }
    void SetFrameGenerationActive(bool active) { frameGenerationActive_ = active; }
    void SetActivePackFeatures(size_t materialMaps, size_t refinedModels) {
        activeMaterialMaps_ = materialMaps;
        activeRefinedModels_ = refinedModels;
    }
    void SetActivePackName(std::string name) { activePackName_ = std::move(name); }
    std::string RowHelp(const DisplaySettings& d, int row) const;
    void SetRenderDimensions(int width, int height) {
        renderWidth_ = width;
        renderHeight_ = height;
    }
    int CalibrationPattern() const { return calibration_; }
    void SetPacks(std::vector<PackChoice> packs, const std::string& currentPath);
    bool TakePackChange(std::string* path);
    std::string SelectedPackPath() const { return packs_.empty() ? std::string{} : packs_[pack_].path; }
    void SetStatus(const std::string& status) { status_ = status; }

    // Layout in cells (16 x 24 px); RowAt: the row under a pointer at game pixel (px, py), -1 = none.
    static float RowY(int row, Page page = kDisplay, int scroll = 0);
    static int RowAt(float px, float py, Page page = kDisplay, int scroll = 0);
    static const std::vector<int>& Rows(Page page);
    static int TabAt(float px, float py);
    static float TabLeft(int page);
    static float TabWidth(int page);
    static bool PreviewButtonAt(float x, float y) { return x >= 550 && x < 630 && y >= 456 && y < 476; }
    // Confirmation choices: 1 resets, 0 cancels, -1 is outside either button.
    static int ResetChoiceAt(float x, float y);
    static const char* Label(int row);
    static std::string ValueText(const DisplaySettings& d, int row);
    // Cycles the value of a row by d steps (wrapping): 1x / 2x / 3x / Fit, Off / On, ...; no-op for Back.
    static void Step(DisplaySettings& d, int row, int delta);
    static void ApplyPreset(DisplaySettings& d, int preset, const port::GraphicsCapabilities* caps = nullptr);
    static std::string PresetName(const DisplaySettings& d, const port::GraphicsCapabilities* caps = nullptr);

   private:
    bool open_ = false;
    bool uiHidden_ = false;
    bool resetConfirm_ = false, resetYes_ = false, graphicsReset_ = false;
    bool resetGameplayConfirm_ = false, gameplayReset_ = false, optionChanged_ = false;
    OptionValues optionValues_;
    DeviceAction deviceAction_ = DeviceAction::None;
    bool nightShiftActive_ = true;
    int cursor_ = kFullscreen;
    Page page_ = kDisplay;
    int scroll_ = 0;
    void KeepCursorVisible();
    game::MenuPointerFocus pointer_;
    std::vector<std::string> monitors_;
    std::vector<PackChoice> packs_;
    int pack_ = 0;
    bool packChanged_ = false;
    std::string status_;
    bool Disabled(int row, const DisplaySettings&) const;
    std::string ShownValue(const DisplaySettings&, int row) const;
    port::GraphicsCapabilities caps_;
    bool curvatureAvailable_ = true;
    bool temporalInputActive_ = false;
    bool raytracingAvailable_ = false;
    bool frameGenerationActive_ = false;
    size_t activeMaterialMaps_ = 0, activeRefinedModels_ = 0;
    std::string activePackName_ = "Original";
    int renderWidth_ = 0, renderHeight_ = 0, calibration_ = -1;
};

}  // namespace play

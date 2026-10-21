#include "port/play/pause_menu.h"

#include <algorithm>
#include <charconv>
#include <fstream>

#include "game/menu_text.h"

namespace play {

namespace {

constexpr float kCellH = 24.0f;    // 0x004ECB84
constexpr float kLabelX = 2.0f;    // cells (the options screen's lines start at cell 2)
constexpr float kConfirmY = 8.0f;  // the question
constexpr float kChoiceY = 10.0f;  // Yes / No
constexpr float kYesX = 12.0f, kNoX = 24.0f;
constexpr uint32_t kSelected = 0x111;  // red, depth 0.9 (the options screen's selected line)
constexpr uint32_t kNormal = 1;
constexpr uint32_t kSndCursor = 0xA9, kSndConfirm = 0x121A9;
constexpr float kModernRowLeft = 104, kModernRowRight = 536, kModernPreviousX = 432;
constexpr float kModernConfirmTop = 274, kModernConfirmBottom = 312;
constexpr float kModernReturnLeft = 128, kModernReturnRight = 304, kModernCancelLeft = 336, kModernCancelRight = 512;

const char* const kLabels[play::PauseMenu::kRowCount] = {"Resume",           "Music",       "Gun Shots",  "Effects + Dialogue",
                                                      "Graphics Options", "Wii Remotes", "Light Guns", "Return to Main Menu"};

}  // namespace

bool Settings::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    for (std::string line; std::getline(f, line);) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        while (!val.empty() && (val.back() == '\r' || val.back() == ' ')) val.pop_back();
        const int v = std::clamp(atoi(val.c_str()), 0, 10);
        int numeric = 0;
        const auto parsed = std::from_chars(val.data(), val.data() + val.size(), numeric);
        const bool integer = parsed.ec == std::errc{} && parsed.ptr == val.data() + val.size();
        auto number = [&](int& field, int low, int high) {
            if (integer && numeric >= low && numeric <= high) field = numeric;
        };
        auto boolean = [&](bool& field) {
            if (integer && (numeric == 0 || numeric == 1)) field = numeric != 0;
        };
        auto& graphics = display.graphics;
        if (key == "music")
            music = v;
        else if (key == "guns")
            guns = v;
        else if (key == "sfx")
            sfx = v;
        else if (key == "input")
            input = val;
        else if (key == "fullscreen")
            display.fullscreen = v != 0;
        else if (key == "fullscreen_mode" && (val == "exclusive" || val == "borderless"))
            display.exclusiveFullscreen = val == "exclusive";
        else if (key == "scale")
            display.windowScale = val == "fit" ? 0 : std::clamp(v, 1, 3);
        else if (key == "aspect")
            display.stretch = val == "stretch";
        else if (key == "viewport")
            number(display.viewport, 0, 4);
        else if (key == "crt")
            display.crt = std::clamp(v, 0, 3);
        else if (key == "render")
            display.renderScale = std::clamp(v, 1, 4);
        else if (key == "blood" && (val == "red" || val == "green"))
            display.redBlood = val == "red";
        else if (key == "msaa" && integer && (numeric == 1 || numeric == 2 || numeric == 4))
            graphics.msaa = numeric;
        else if (key == "anisotropy" && integer && (numeric == 1 || numeric == 2 || numeric == 4 || numeric == 8 || numeric == 16))
            graphics.anisotropy = numeric;
        else if (key == "texture_filter")
            number(graphics.textureFilter, 0, 2);
        else if (key == "post_aa")
            number(graphics.postAA, 0, 3);
        else if (key == "spatial_upscaler")
            number(graphics.spatialUpscaler, 0, 1);
        else if (key == "sharpening")
            number(graphics.sharpening, 0, 3);
        else if (key == "alpha_coverage")
            boolean(graphics.alphaToCoverage);
        else if (key == "vsync")
            number(graphics.syncInterval, 0, 1);
        else if (key == "allow_tearing")
            boolean(graphics.allowTearing);
        else if (key == "low_latency")
            boolean(graphics.lowLatency);
        else if (key == "hdr_output")
            boolean(graphics.hdrOutput);
        else if (key == "output_resolution_hud")
            boolean(graphics.outputResolutionHud);
        else if (key == "temporal_aa")
            boolean(graphics.temporalAA);
        else if (key == "hdr_paper_white")
            number(graphics.hdrPaperWhite, 80, 400);
        else if (key == "downsample")
            boolean(graphics.downsample);
        else if (key == "brightness")
            number(graphics.brightness, 0, 100);
        else if (key == "gamma")
            number(graphics.gamma, 0, 100);
        else if (key == "contrast")
            number(graphics.contrast, 0, 100);
        else if (key == "saturation")
            number(graphics.saturation, 0, 100);
        else if (key == "bloom")
            number(graphics.bloom, 0, 3);
        else if (key == "color_grade")
            number(graphics.colorGrade, 0, 3);
        else if (key == "per_pixel_lighting")
            boolean(graphics.perPixelLighting);
        else if (key == "soft_particles")
            number(graphics.softParticles, 0, 3);
        else if (key == "material_maps")
            boolean(graphics.materialMaps);
        else if (key == "ambient_occlusion")
            number(graphics.ambientOcclusion, 0, 3);
        else if (key == "shadow_maps")
            number(graphics.shadowMaps, 0, 3);
        else if (key == "volumetric_fog")
            number(graphics.volumetricFog, 0, 3);
        else if (key == "reflections")
            number(graphics.reflections, 0, 3);
        else if (key == "cinematic_focus")
            number(graphics.cinematicFocus, 0, 3);
        else if (key == "mesh_refinement")
            number(graphics.meshRefinement, 0, 3);
        else if (key == "crt_mask")
            number(graphics.crtMask, 0, 3);
        else if (key == "crt_curvature")
            number(graphics.crtCurvature, 0, 3);
        else if (key == "crt_glow")
            number(graphics.crtGlow, 0, 3);
        else if (key == "resolution_mode")
            number(display.resolutionMode, 0, 3);
        else if (key == "dynamic_resolution")
            boolean(display.dynamicResolution);
        else if (key == "gpu_budget_fps" && integer && (numeric == 60 || numeric == 120 || numeric == 144))
            display.gpuBudgetFps = numeric;
        else if (key == "renderer")
            number(display.renderer, 0, 2);
        else if (key == "temporal_upscaler")
            number(display.temporalUpscaler, 0, 4);
        else if (key == "temporal_quality")
            number(display.temporalQuality, 0, 3);
        else if (key == "frame_generation")
            number(display.frameGeneration, 0, 2);
        else if (key == "ray_shadows")
            boolean(display.rayShadows);
        else if (key == "render_interpolation")
            boolean(display.renderInterpolation);
        else if (key == "integer_scale")
            boolean(display.integerScale);
        else if (key == "frame_cap" && integer &&
                 (numeric == 0 || numeric == 30 || numeric == 60 || numeric == 120 || numeric == 144 || numeric == 165 || numeric == 240))
            display.frameCap = numeric;
        else if (key == "monitor")
            number(display.monitor, 0, 32);
        else if (key == "fps_overlay")
            boolean(display.statistics);
        else if (key == "menu" && (val == "classic" || val == "nightshift"))
            display.nightShift = val == "nightshift";
        else if (key == "menu_opacity")
            number(display.menuOpacity, 25, 100);
        else if (key == "crosshairs")
            boolean(display.showCrosshairs);
        else if (key == "assets")
            assets = val;
        else if (key == "lobby")
            lobby = val;
    }
    return true;
}

bool Settings::Save(const std::string& path) const {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << "music=" << music << "\nguns=" << guns << "\nsfx=" << sfx << "\n";
    if (!input.empty()) f << "input=" << input << "\n";
    f << "fullscreen=" << (display.fullscreen ? 1 : 0) << "\n";
    f << "fullscreen_mode=" << (display.exclusiveFullscreen ? "exclusive" : "borderless") << "\n";
    if (display.windowScale == 0)
        f << "scale=fit\n";
    else
        f << "scale=" << display.windowScale << "\n";
    f << "aspect=" << (display.stretch ? "stretch" : "4:3") << "\ncrt=" << display.crt << "\nrender=" << display.renderScale << "\n";
    f << "blood=" << (display.redBlood ? "red" : "green") << "\n";
    f << "viewport=" << display.viewport << "\n";
    const auto& g = display.graphics;
    f << "msaa=" << g.msaa << "\nanisotropy=" << g.anisotropy << "\ntexture_filter=" << g.textureFilter << "\npost_aa=" << g.postAA
      << "\nspatial_upscaler=" << g.spatialUpscaler << "\nsharpening=" << g.sharpening << "\nalpha_coverage=" << g.alphaToCoverage
      << "\nvsync=" << g.syncInterval << "\nallow_tearing=" << g.allowTearing << "\nlow_latency=" << g.lowLatency
      << "\ndownsample=" << g.downsample << "\nbrightness=" << g.brightness << "\ngamma=" << g.gamma << "\ncontrast=" << g.contrast
      << "\nsaturation=" << g.saturation << "\nbloom=" << g.bloom << "\ncolor_grade=" << g.colorGrade
      << "\nper_pixel_lighting=" << g.perPixelLighting << "\nsoft_particles=" << g.softParticles << "\nmaterial_maps=" << g.materialMaps
      << "\nambient_occlusion=" << g.ambientOcclusion << "\nshadow_maps=" << g.shadowMaps << "\nvolumetric_fog=" << g.volumetricFog
      << "\nreflections=" << g.reflections << "\ncinematic_focus=" << g.cinematicFocus << "\nmesh_refinement=" << g.meshRefinement
      << "\ncrt_mask=" << g.crtMask << "\ncrt_curvature=" << g.crtCurvature << "\ncrt_glow=" << g.crtGlow
      << "\nresolution_mode=" << display.resolutionMode << "\ndynamic_resolution=" << display.dynamicResolution
      << "\ngpu_budget_fps=" << display.gpuBudgetFps << "\nrenderer=" << display.renderer
      << "\ntemporal_upscaler=" << display.temporalUpscaler << "\ntemporal_quality=" << display.temporalQuality
      << "\nframe_generation=" << display.frameGeneration << "\nray_shadows=" << display.rayShadows
      << "\nrender_interpolation=" << display.renderInterpolation << "\ninteger_scale=" << display.integerScale
      << "\nframe_cap=" << display.frameCap << "\nmonitor=" << display.monitor << "\nhdr_output=" << g.hdrOutput
      << "\nhdr_paper_white=" << g.hdrPaperWhite << "\noutput_resolution_hud=" << g.outputResolutionHud << "\ntemporal_aa=" << g.temporalAA
      << "\nfps_overlay=" << display.statistics << "\nmenu=" << (display.nightShift ? "nightshift" : "classic")
      << "\nmenu_opacity=" << display.menuOpacity << "\ncrosshairs=" << display.showCrosshairs << "\n";
    if (!assets.empty()) f << "assets=" << assets << "\n";
    if (!lobby.empty()) f << "lobby=" << lobby << "\n";
    return static_cast<bool>(f);
}

void PauseMenu::Open() {
    open_ = true;
    cursor_ = kResume;
    confirm_ = confirmYes_ = false;
    pointer_ = {};
    display_.Close();
}

float PauseMenu::RowY(int row) {
    switch (row) {
        case kResume:
            return 5.0f;
        case kMusic:
            return 7.0f;
        case kGuns:
            return 8.0f;
        case kSfx:
            return 9.0f;
        case kDisplay:
            return 11.0f;
        case kWii:
            return 12.0f;
        case kLightguns:
            return 13.0f;
        case kReturn:
            return 14.0f;
        default:
            return 0.0f;
    }
}

int PauseMenu::RowAt(float px, float py) {
    for (int r = 0; r < kRowCount; ++r)
        if (game::MenuTextRowHit(px, py, RowY(r))) return r;
    return -1;
}

int PauseMenu::ConfirmAt(float px, float py) {
    if (!game::MenuTextRowHit(px, py, kChoiceY)) return -1;
    if (px >= (kYesX - 2.0f) * 16.0f && px < (kYesX + 5.0f) * 16.0f) return 1;
    if (px >= (kNoX - 2.0f) * 16.0f && px < (kNoX + 4.0f) * 16.0f) return 0;
    return -1;
}

float PauseMenu::ModernRowTop(int row) { return 128.f + 32.f * row; }

int PauseMenu::ModernRowAt(float px, float py) {
    if (px < kModernRowLeft || px >= kModernRowRight) return -1;
    for (int row = 0; row < kRowCount; ++row) {
        const float top = ModernRowTop(row);
        if (py >= top - 2 && py < top + 26) return row;
    }
    return -1;
}

int PauseMenu::ModernConfirmAt(float px, float py) {
    if (py < kModernConfirmTop || py >= kModernConfirmBottom) return -1;
    if (px >= kModernReturnLeft && px < kModernReturnRight) return 1;
    if (px >= kModernCancelLeft && px < kModernCancelRight) return 0;
    return -1;
}

int* PauseMenu::Volume(Settings& s, int row) {
    switch (row) {
        case kMusic:
            return &s.music;
        case kGuns:
            return &s.guns;
        case kSfx:
            return &s.sfx;
        default:
            return nullptr;
    }
}

PauseMenu::Action PauseMenu::Update(const Input& in, Settings& s, std::vector<uint32_t>& sounds) {
    if (!open_) return Action::None;
    if (display_.IsOpen() && (display_.UiHidden() || display_.ResetConfirming())) {
        display_.Update(in, s.display, sounds);  // restoring preview or cancelling reset must not resume the game
        return Action::None;
    }
    if (in.start) {  // the original resumes with the pause button
        sounds.push_back(kSndConfirm);
        display_.Close();
        return Action::Resume;
    }
    if (display_.IsOpen()) {  // the display settings page takes the input; closing it returns to the rows
        display_.Update(in, s.display, sounds);
        return Action::None;
    }
    if (!confirm_) {
        const int under = in.pointer ? modern_ ? ModernRowAt(in.px, in.py) : RowAt(in.px, in.py) : -1;
        if (pointer_.Update(in.pointer, in.px, in.py, in.trigger || in.reload) && under >= 0 && under != cursor_) {
            cursor_ = under;
            sounds.push_back(kSndCursor);
        }
        if (in.pressed & 0x10) {
            cursor_ = (cursor_ + kRowCount - 1) % kRowCount;
            sounds.push_back(kSndCursor);
        }
        if (in.pressed & 0x20) {
            cursor_ = (cursor_ + 1) % kRowCount;
            sounds.push_back(kSndCursor);
        }
        int* vol = Volume(s, cursor_);
        const bool overCursor = under == cursor_;
        int d = 0;
        if (in.pressed & 0x80) ++d;
        if (in.pressed & 0x40) --d;
        if (vol && overCursor) {
            if (in.trigger) d += game::MenuPointerStep(in.px, modern_ ? kModernPreviousX : 520);
            if (in.reload) --d;
        }
        if (vol && d != 0) {
            const int nv = std::clamp(*vol + d, 0, 10);
            if (nv != *vol) {
                *vol = nv;
                sounds.push_back(kSndCursor);
            }
        }
        const bool select = (in.pressed & 0xC) != 0 || (in.trigger && overCursor && !vol);
        const bool back = (in.pressed & 0x2) != 0 || (in.reload && !vol);
        if (back) {
            sounds.push_back(kSndConfirm);
            return Action::Resume;
        }
        if (select) {
            if (cursor_ == kResume) {
                sounds.push_back(kSndConfirm);
                return Action::Resume;
            }
            if (cursor_ == kWii) return Action::WiiRemotes;
            if (cursor_ == kLightguns) return Action::Lightguns;
            if (cursor_ == kDisplay) {
                display_.Open();
                sounds.push_back(kSndConfirm);
            }
            if (cursor_ == kReturn) {
                confirm_ = true;
                confirmYes_ = false;  // No is the safe default
                pointer_ = {};
                sounds.push_back(kSndConfirm);
            }
        }
        return Action::None;
    }
    // the confirmation: Yes / No
    const int under = in.pointer ? modern_ ? ModernConfirmAt(in.px, in.py) : ConfirmAt(in.px, in.py) : -1;
    if (pointer_.Update(in.pointer, in.px, in.py, in.trigger || in.reload) && under >= 0 && (under == 1) != confirmYes_) {
        confirmYes_ = under == 1;
        sounds.push_back(kSndCursor);
    }
    if (in.pressed & 0xF0) {
        confirmYes_ = !confirmYes_;
        sounds.push_back(kSndCursor);
    }
    const bool select = (in.pressed & 0xC) != 0 || (in.trigger && under == (confirmYes_ ? 1 : 0));
    const bool back = (in.pressed & 0x2) != 0 || in.reload;
    if (back) {
        confirm_ = false;
        sounds.push_back(kSndCursor);
        return Action::None;
    }
    if (select) {
        sounds.push_back(kSndConfirm);
        if (confirmYes_) return Action::ReturnToTitle;
        confirm_ = false;
    }
    return Action::None;
}

void PauseMenu::Sprites(const game::ExeImage& exe, int pausePlayer, const Settings& s, std::vector<game::SpriteDef>& out) const {
    if (!open_) return;
    if (display_.IsOpen()) {  // the display settings page is the whole screen (its own background and heading)
        display_.Sprites(exe, s.display, out);
        return;
    }
    game::MenuBackground(out);
    // the original's pause label (0x0040E500: 0x8E7 P1 / 0x8E8 P2, depth 0.6, flags 0x70A) as the title, where the
    // options screen has its title
    game::SpriteDef t;
    t.id = pausePlayer == 1 ? 0x8E8 : 0x8E7;
    t.x = 320.0f;
    t.y = 56.0f;
    t.depth = 0.6f;
    t.flags = 0x70A;
    out.push_back(t);
    if (!confirm_) {
        for (int r = 0; r < kRowCount; ++r)
            game::MenuText(exe, kLabelX, RowY(r), r == kReturn && networkRoom_ ? "Return to Lobby" : kLabels[r],
                           r == cursor_ ? kSelected : kNormal, out);
        if (cursor_ >= kMusic && cursor_ <= kSfx) game::MenuAdjustArrows(520, 620, RowY(cursor_) * kCellH + 22, out);
        game::MenuTextScaled(exe, 32, 370, "Click or shoot   Reload to reverse", 0, 0.8f, out);
        // (the volume values are drawn by the caller with game::MenuNum at cell x 35, the options screen's number column)
    } else {
        game::MenuText(exe, 0.0f, kConfirmY, networkRoom_ ? "Return both players to Lobby?" : "Return to the Main Menu?", 2, out);
        game::MenuText(exe, kYesX, kChoiceY, "Yes", confirmYes_ ? kSelected : kNormal, out);
        game::MenuText(exe, kNoX, kChoiceY, "No", confirmYes_ ? kNormal : kSelected, out);
    }
}

void PauseMenu::ModernHud(int pausePlayer, const Settings& s, game::WorldHud& hud) const {
    if (!open_) return;
    if (display_.IsOpen()) {
        display_.ModernHud(s.display, hud);
        return;
    }
    hud.nightShiftPage = 8;
    hud.nightShiftRow = cursor_;
    hud.menuPanels = {{88, 52, 552, 448, 0xF4090D10}, {104, 118, 536, 120, 0xFF92272D}};
    hud.menuText.clear();
    auto text = [&](float x, float y, const std::string& value, float scale, uint32_t color = 0xFFECE7DC) {
        hud.menuText.push_back({x, y, value, scale, color});
    };
    text(112, 66, "PAUSED", 1.05f);
    const std::string player = pausePlayer == 1 ? "PLAYER 2" : "PLAYER 1";
    text(112, 103, (networkRoom_ ? "ONLINE CO-OP / PAUSED BY " : "PAUSED BY ") + player, .4f, 0xFFC1BAAC);
    if (confirm_) {
        text(112, 160, networkRoom_ ? "RETURN TO LOBBY?" : "RETURN TO MAIN MENU?", .65f);
        text(112, 204, networkRoom_ ? "Return both players to the lobby?" : "End this run and return to the main menu?", .5f);
        for (int choice = 0; choice < 2; ++choice) {
            const bool yes = choice == 1;
            const float left = yes ? kModernReturnLeft : kModernCancelLeft;
            const float right = yes ? kModernReturnRight : kModernCancelRight;
            hud.menuPanels.push_back({left, kModernConfirmTop, right, kModernConfirmBottom,
                                      yes == confirmYes_ ? 0xDE8F2026u : 0xB82B3033u});
            text(left + 22, kModernConfirmTop + 9, yes ? "RETURN" : "CANCEL", .6f);
        }
        text(112, 403, "RELOAD / BACK: CANCEL", .4f, 0xFFC1BAAC);
        return;
    }
    for (int row = 0; row < kRowCount; ++row) {
        const float top = ModernRowTop(row);
        if (row == cursor_)
            hud.menuPanels.push_back({kModernRowLeft, top - 2, kModernRowRight, top + 26, 0xCC8F2026});
        else
            hud.menuPanels.push_back({kModernRowLeft, top + 26, kModernRowRight, top + 27, 0x354E514E});
        text(112, top, row == kReturn && networkRoom_ ? "Return to Lobby" : kLabels[row], .55f);
        const int* volume = row == kMusic ? &s.music : row == kGuns ? &s.guns : row == kSfx ? &s.sfx : nullptr;
        if (volume) {
            text(462, top, std::to_string(std::clamp(*volume, 0, 10) * 10) + "%", .55f);
            if (row == cursor_) {
                text(kModernPreviousX, top, "<", .55f);
                text(518, top, ">", .55f);
            }
        }
    }
    const bool volume = cursor_ >= kMusic && cursor_ <= kSfx;
    text(112, 403, volume ? "LEFT / RIGHT: VOLUME   RELOAD: LOWER" : "SELECT / SHOOT: OPEN   RELOAD / BACK: RESUME", .4f, 0xFFC1BAAC);
}

}  // namespace play

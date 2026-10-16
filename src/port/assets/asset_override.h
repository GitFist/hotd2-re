#pragma once

// Optional asset pack ("remaster", tools/remaster.py, notes/agents/remaster.md): a directory whose files replace the
// originals when present. Textures: <dir>/tex/<tex file stem>/<index>.png (AssetStore), BMP front screens:
// <dir>/bmp/<file>.png (FrontScreens), sounds: <dir>/<game-relative path>.wav (audio backends). Selected by the
// HOTD2_ASSET_DIR environment variable or SetAssetDir (play.exe: "assets=" in hotd2re_settings.ini; the environment
// variable wins). Without either, every lookup fails and the port behaves exactly as before.

#include <string>

namespace port {

// The pack directory, or empty.
const std::string& AssetDir();
// Sets the pack directory unless HOTD2_ASSET_DIR is set.
void SetAssetDir(const std::string& dir);

// Full path of <AssetDir()>/<rel> when it exists, else empty. rel may use '\\' or '/'.
std::string AssetOverride(const std::string& rel);

// How many overrides have been found so far (diagnostics).
int AssetOverrideHits();

// Game-relative path ("Sound\\SE\\x.wav") -> a path under gameDir with '/' separators (works on every platform; the
// data files keep the original names, so case-sensitive file systems need the original upper / lower case).
std::string GamePath(const std::string& gameDir, const std::string& rel);

// The sound file to open: the pack's copy when it has one, else the game's.
std::string SoundPath(const std::string& gameDir, const std::string& rel);

}  // namespace port

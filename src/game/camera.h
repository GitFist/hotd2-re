#pragma once

// HOTD2 camera paths (cam\*.bin), see notes/formats.md. From Cam_LoadFile 0x00403E70, Cam_Evaluate 0x004041E0,
// Cam_Curve 0x004040F0, Cam_LookAngles 0x00403AC0 / Cam_DirToAngles 0x004016B0, Cam_BuildViews 0x00401F40.
// Validated against engine output captured from the original (tests/cam_test.cpp).

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "anim.h"  // Mat4
#include "exe_image.h"

namespace game {

struct CameraFileEntry {
    int index;
    std::string name;              // file in cam\  (0x004D1BC8[n])
    std::vector<int16_t> cameraIds;  // 0x004C470C[n], count 0x004C476C[n]
};
std::vector<CameraFileEntry> ListCameraFiles(const ExeImage& exe);

class CameraFile {
public:
    bool Load(std::vector<uint8_t> data, int count);
    int Count() const { return count_; }
    // Channel words of camera k: 0-2 position, 3-5 target, 6 roll, 7 (unused by Cam_Evaluate).
    void Channels(int k, int32_t out[8]) const;
    // Mirror of Cam_Curve 0x004040F0 at time t (binary search + cubic Hermite).
    float EvalCurve(int32_t channelWord, float t) const;
    // Mirror of Cam_Evaluate: position, target, roll (roll only meaningful when the game's flag 0x009A21B0 is set).
    void Evaluate(int k, float t, float pos[3], float target[3], int* roll) const;
    // Last key time of camera k's position channels (path length in frames).
    float Duration(int k) const;

private:
    std::vector<uint8_t> data_;
    int count_ = 0;
    size_t base_ = 0;
};

// Every camera path by camera id, the way Cam_Evaluate 0x004041E0 finds them (file of id 0x004C479C[id], files
// 0x004D1BC8 loaded by Cam_LoadFile 0x00403E70 with the id lists 0x004C470C).
class CameraBank {
public:
    // read(name) returns the bytes of cam\<name> (empty when missing; those ids stay unknown).
    bool Load(const ExeImage& exe, const std::function<std::vector<uint8_t>(const std::string&)>& read);
    // Appends exact imported curves under ids assigned by the offline converter.
    bool AddPreparedFile(std::vector<uint8_t> bytes, const std::vector<int>& ids);
    bool Has(int id) const { return ids_.count(id) != 0; }
    // Cam_Evaluate: pos/target; roll = curve 6 when the roll channel is on (0x009A21B0), else 0.
    bool Evaluate(int id, float t, float pos[3], float target[3], int* roll, bool rollEnabled) const;

private:
    std::vector<std::unique_ptr<CameraFile>> files_;
    std::map<int, std::pair<const CameraFile*, int>> ids_;
};

// Cam_LookAngles 0x00403AC0 / Cam_DirToAngles 0x004016B0: engine angles (0x10000 = 360 degrees) looking from pos at
// target, with the game's 24-bit FPU precision (game::DirToAngles).
void LookAngles(const float pos[3], const float target[3], int* pitch, int* yaw);

// Cam_BuildViews: V = T(-pos) * Ry(-yaw) * Rx(-pitch) * Rz(-roll)  (row vectors), built with the engine's matrix-stack
// operations (the game first normalises the angles, see game::BuildContextViews).
Mat4 ViewMatrix(const float pos[3], int pitch, int yaw, int roll);

// Device transforms observed in the original (captured with IDirect3DDevice7::GetTransform):
// view = diag(1, 1, -1, 1); projection fixed for the 640x480 mode (near 0.8, far ~8000, ~41.1 deg vertical FOV).
Mat4 DeviceView();
Mat4 DeviceProjection();

}  // namespace game

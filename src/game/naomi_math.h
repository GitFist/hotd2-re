#pragma once

#include <cstdint>

namespace game::naomi {

// The original game saves FPSCR=0x00140001: single precision, round toward
// zero, denormal inputs/results flushed to zero. These opt-in primitives do
// not change the host rounding mode or the PC simulation's arithmetic.
float Add(float a,float b);
float Sub(float a,float b);
float Mul(float a,float b);
float Div(float a,float b);
float Fmac(float a,float b,float c); // one final rounding of a*b+c
float Sqrt(float a);
float FromInt(int32_t a);

// Original SDK scalar angle routines, in 65536 units per revolution.
int ArcTan(float x);             // 0x0C0B36D0, unsigned 16-bit result
int ArcTan2(float y,float x);    // 0x0C0B3770
float Sin(int angle);           // 0x0C0B3DE0
float Cos(int angle);           // 0x0C0B3840
float Tan(int angle);           // 0x0C0B4050

// Original SDK projection X/Y scales for FOV0x1D3C, aspect0x3FAAAAAA.
// This does not set depth clipping or alter the separate aiming focal.
void ProjectionXY(float* x,float* y);

} // namespace game::naomi

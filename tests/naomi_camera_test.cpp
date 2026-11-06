// naomi_camera_test GAME_DIR: original native save-state camera goldens plus
// source arithmetic/SDK boundary cases. Never changes World or the host fenv.
#include "game/camera.h"
#include "game/naomi_math.h"
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
int checks=0;
void Require(bool ok,const char* why) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
uint32_t Bits(float x) {return std::bit_cast<uint32_t>(x);}
float Value(uint32_t x) {return std::bit_cast<float>(x);}
void Exact(float x,uint32_t expected,const char* why) {
    ++checks;
    if(Bits(x)!=expected) {
        std::fprintf(stderr,"FAIL: %s got=%08X expected=%08X (%.9g)\n",why,Bits(x),expected,x);std::exit(1);
    }
}
std::vector<uint8_t> Read(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary);return {(std::istreambuf_iterator<char>(f)),{}};
}
void Math() {
    using namespace game::naomi;
    const int rounding=std::fegetround();
    Exact(Add(1.0f,std::ldexp(1.0f,-25)),0x3f800000,"RZ positive addition");
    Exact(Sub(1.0f,std::ldexp(1.0f,-25)),0x3f7fffff,"RZ subtraction below exact float");
    Exact(Div(1.0f,3.0f),0x3eaaaaaa,"RZ division");
    Exact(Mul(Value(0xff7fffff),2.0f),0xff7fffff,"RZ overflow saturates finite");
    Exact(Mul(Value(0x80800000),0.5f),0x80000000,"DN result signed zero");
    Exact(Add(Value(1),1.0f),0x3f800000,"DN input flushed");
    Exact(Fmac(1.0f,1.0f,-std::ldexp(1.0f,-100)),0x3f7fffff,"FMAC retains tiny opposite addend");
    Exact(Fmac(Value(0x3f800001),Value(0x3f7ffffe),-1.0f),0xa8800000,"FMAC rounds only once");
    Exact(Sqrt(2.0f),0x3fb504f3,"RZ square root");
    Exact(FromInt(16777219),0x4b800001,"RZ integer conversion");
    Require(std::fegetround()==rounding,"native helpers changed host rounding mode");
    const std::array<std::array<int,3>,8> cases={{{0,1,0},{1,0,0x4000},{0,-1,0x8000},{-1,0,0xc000},
        {1,1,0x2000},{1,-1,0x6000},{-1,-1,0xa000},{-1,1,0xe000}}};
    for(const auto& c:cases)Require(ArcTan2(static_cast<float>(c[0]),static_cast<float>(c[1]))==c[2],"native ArcTan2 quadrant");
    // Native SDK trig intentionally uses its scalar rational approximation.
    // Exact axes and wrap are independently specified by the angle convention.
    Require(std::fabs(Sin(0))<1e-6f && std::fabs(Cos(0)-1.0f)<1e-6f,"SDK trig origin");
    Require(std::fabs(Sin(0x4000)-1.0f)<1e-6f && std::fabs(Cos(0x4000))<1e-6f,"SDK trig quarter turn");
    Exact(Sin(1234),Bits(Sin(1234+65536)),"SDK trig angle wrap");
    Exact(Tan(0x4000),0x7f7fffff,"SDK tangent quarter-turn saturation");
    Exact(Tan(1234),Bits(Tan(1234+65536)),"SDK tangent angle wrap");
    Exact(Div(240.0f,Tan(0xe9d)),0x44200d4f,"native captured aiming focal");
    // All three original retained states store the same raw SDK projection
    // at *(0C9C28D0)=0C9C29A0, diagonal0/5. Independent TA/source-geometry
    // calibration also yields focal640.0212, center320/240.
    float x=0,y=0;ProjectionXY(&x,&y);
    Exact(x,0x40000117,"native captured projection X scale");
    Exact(y,0x402aac1f,"native captured projection Y scale");
}
void Boundaries() {
    // Four linear Hermite keys, seven channels including roll; this exercises
    // the original padded binary search on both sides of exact key times.
    std::vector<uint8_t> bytes(108);
    auto put=[&](size_t off,const auto& v){std::memcpy(bytes.data()+off,&v,sizeof(v));};
    put(0,uint32_t{8});put(4,uint32_t{0xffffffff});
    for(int i=0;i<8;++i)put(8+i*4,int32_t{8});
    put(40,uint16_t{4});put(42,uint16_t{2});
    for(int i=0;i<4;++i) {
        put(44+i*16,float(i*10));put(48+i*16,float(i*10));
        put(52+i*16,1.0f);put(56+i*16,1.0f);
    }
    game::CameraFile file;Require(file.Load(bytes,1),"synthetic camera load");
    for(float t:{0.0f,5.0f,10.0f,15.0f,20.0f,25.0f,30.0f}) {
        float p[3],target[3];int roll=-1;file.EvaluateNaomi(0,t,p,target,&roll);
        for(float v:p)Exact(v,Bits(t),"Hermite position key/segment boundary");
        for(float v:target)Exact(v,Bits(t),"Hermite target key/segment boundary");
        Require(roll==static_cast<int>(t),"Hermite roll channel");
    }
    game::CameraBank bank;Require(bank.AddPreparedFile(std::move(bytes),{100}),"prepared camera load");
    float p[3]={1,2,3},target[3]={4,5,6};int roll=7;
    Require(!bank.EvaluateNaomi(101,0,p,target,&roll,true),"missing native camera lookup");
    Require(p[0]==1 && target[2]==6 && roll==7,"missing camera changed outputs");
    Require(bank.EvaluateNaomi(100,15,p,target,&roll,false) && roll==0,"disabled native roll");
}
void Captured(const char* gameDir) {
    const auto root=std::filesystem::path(gameDir);game::ExeImage exe;
    Require(exe.Load((root/"Hod2.exe").string()),"PC reference executable");
    game::CameraBank bank;Require(bank.Load(exe,[&](const std::string& n){return Read(root/"cam"/n);}),"camera bank");
    float pcBefore[6],pcAfter[6],p[3],target[3];int roll=0;
    Require(bank.Evaluate(0x22,319,pcBefore,pcBefore+3,&roll,false),"PC camera lookup");
    Require(bank.EvaluateNaomi(0x22,319,p,target,&roll,false),"native church camera lookup");
    // church.state: RAM0C3CFDB4 position /0C3CFDCC target, cameraID18,
    // tick319. Explicit source loader maps native cp_st1[2] to PCglobal22.
    constexpr uint32_t church[6]={0xc160c1fb,0x41989dfb,0x431ee240,0xc17b985e,0x4302f686,0x42878cb9};
    for(int i=0;i<3;++i){Exact(p[i],church[i],"native captured church position");Exact(target[i],church[3+i],"native captured church target");}
    int pitch=0,yaw=0;game::NaomiLookAngles(p,target,&pitch,&yaw);
    Require(pitch==9255 && yaw==192,"native captured church SDK look angles");
    Require(bank.EvaluateNaomi(0x22,470,p,target,&roll,false),"native encounter camera lookup");
    // first_encounter.state context's position is the same curve; target and
    // angles have gameplay tracking applied, so they are not curve goldens.
    constexpr uint32_t encounter[3]={0xc217872b,0x41733333,0x4305be0d};
    for(int i=0;i<3;++i)Exact(p[i],encounter[i],"native captured first-encounter position");
    const float tracked[3]={Value(0x42674603),Value(0x41decaf3),Value(0x431fd8c5)};
    game::NaomiLookAngles(p,tracked,&pitch,&yaw);
    Require(pitch==1323 && yaw==-19162,"native captured tracked encounter SDK look angles");
    Require(bank.Evaluate(0x22,319,pcAfter,pcAfter+3,&roll,false),"PC camera lookup after native math");
    Require(std::memcmp(pcBefore,pcAfter,sizeof(pcBefore))==0,"opt-in native math changed PC evaluation");
    Require(Bits(pcBefore[1])!=church[1],"native golden fails to distinguish existing PC math");
}
}
int main(int argc,char** argv) {
    Require(argc==2,"usage: naomi_camera_test GAME_DIR");
    Math();Boundaries();Captured(argv[1]);
    std::printf("PASS: %d native camera/math checks; original church/encounter raw-bit goldens\n",checks);
}

#include "naomi_math.h"

#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>

namespace game::naomi {
namespace {
constexpr uint32_t kSign=0x80000000u,kInf=0x7f800000u,kMax=0x7f7fffffu,kNaN=0x7fc00000u;
float Float(uint32_t bits) {return std::bit_cast<float>(bits);}
struct Number {uint32_t bits,sign,sig;int exponent;bool inf,nan;};
Number Decode(float x) {
    const auto bits=std::bit_cast<uint32_t>(x),e=(bits>>23)&255u;
    return {bits,bits&kSign,e && e!=255?((bits&0x7fffffu)|0x800000u):0u,
            static_cast<int>(e)-150,e==255 && !(bits&0x7fffffu),e==255 && (bits&0x7fffffu)};
}
float Normalize(uint32_t sign,uint64_t sig,int exponent) {
    if(!sig)return Float(sign);
    const int top=63-std::countl_zero(sig),e=top+exponent;
    if(e < -126)return Float(sign); // FPSCR.DN
    if(e > 127)return Float(sign|kMax); // RZ overflow
    const auto mant=static_cast<uint32_t>(top>23?sig>>(top-23):sig<<(23-top));
    return Float(sign|(static_cast<uint32_t>(e+127)<<23)|(mant&0x7fffffu));
}

// A normal float is sig*2^exponent, exponent>=-149. An exact product's
// exponent is >=-298 and its highest possible bit is 553 in this fixed scale.
// Nine limbs retain even tiny opposite-sign addends at exact float boundaries;
// double intermediates alone would lose those before the RZ decision.
using Wide=std::array<uint64_t,9>;
Wide Term(uint64_t sig,int exponent) {
    Wide w{};if(!sig)return w;
    const unsigned shift=static_cast<unsigned>(exponent+298),i=shift/64,r=shift%64;
    w[i]=sig<<r;if(r && i+1<w.size())w[i+1]=sig>>(64-r);
    return w;
}
int Compare(const Wide& a,const Wide& b) {
    for(size_t i=a.size();i--;)if(a[i]!=b[i])return a[i]>b[i]?1:-1;
    return 0;
}
Wide Sum(Wide a,const Wide& b) {
    uint64_t carry=0;
    for(size_t i=0;i<a.size();++i) {
        const auto v=a[i]+b[i],c1=static_cast<uint64_t>(v<a[i]);
        const auto v2=v+carry,c2=static_cast<uint64_t>(v2<v);
        a[i]=v2;carry=c1|c2;
    }
    return a;
}
Wide Difference(Wide a,const Wide& b) {
    uint64_t borrow=0;
    for(size_t i=0;i<a.size();++i) {
        const auto v=a[i]-b[i],b1=static_cast<uint64_t>(a[i]<b[i]);
        const auto v2=v-borrow,b2=static_cast<uint64_t>(v<borrow);
        a[i]=v2;borrow=b1|b2;
    }
    return a;
}
float RoundWide(const Wide& w,uint32_t sign) {
    size_t i=w.size();while(i && !w[i-1])--i;
    if(!i)return Float(sign);
    const int top=static_cast<int>((i-1)*64)+63-std::countl_zero(w[i-1]);
    const int e=top-298;
    if(e < -126)return Float(sign);
    if(e > 127)return Float(sign|kMax);
    const unsigned shift=static_cast<unsigned>(top-23),limb=shift/64,r=shift%64;
    uint64_t sig=w[limb]>>r;
    if(r && limb+1<w.size())sig|=w[limb+1]<<(64-r);
    return Float(sign|(static_cast<uint32_t>(e+127)<<23)|(static_cast<uint32_t>(sig)&0x7fffffu));
}
}

float Add(float a,float b) {
    auto x=Decode(a),y=Decode(b);
    if(x.nan || y.nan)return Float(kNaN);
    if(x.inf || y.inf) {
        if(x.inf && y.inf && x.sign!=y.sign)return Float(kNaN);
        return Float((x.inf?x.sign:y.sign)|kInf);
    }
    if(!x.sig && !y.sig)return Float(x.sign&y.sign);
    if(!x.sig)return Float(y.sig?y.bits:y.sign);
    if(!y.sig)return Float(x.bits);
    if(x.exponent<y.exponent)std::swap(x,y);
    const int delta=x.exponent-y.exponent;
    if(delta>39) {
        if(x.sign==y.sign)return Float(x.bits);
        const auto magnitude=(x.bits&~kSign)-1;
        return Float(x.sign|(magnitude<0x800000u?0u:magnitude));
    }
    const uint64_t hi=static_cast<uint64_t>(x.sig)<<delta,lo=y.sig;
    if(x.sign==y.sign)return Normalize(x.sign,hi+lo,y.exponent);
    if(hi==lo)return 0.0f;
    return hi>lo?Normalize(x.sign,hi-lo,y.exponent):Normalize(y.sign,lo-hi,y.exponent);
}
float Sub(float a,float b) {return Add(a,Float(std::bit_cast<uint32_t>(b)^kSign));}
float Mul(float a,float b) {
    const auto x=Decode(a),y=Decode(b);const auto sign=x.sign^y.sign;
    if(x.nan || y.nan || (x.inf && !y.sig && !y.inf) || (y.inf && !x.sig && !x.inf))return Float(kNaN);
    if(x.inf || y.inf)return Float(sign|kInf);
    return Normalize(sign,static_cast<uint64_t>(x.sig)*y.sig,x.exponent+y.exponent);
}
float Div(float a,float b) {
    const auto x=Decode(a),y=Decode(b);const auto sign=x.sign^y.sign;
    if(x.nan || y.nan || (x.inf && y.inf) || (!x.sig && !y.sig && !x.inf && !y.inf))return Float(kNaN);
    if(x.inf || (!y.sig && !y.inf))return Float(sign|kInf);
    if(!x.sig || y.inf)return Float(sign);
    const int shift=x.sig<y.sig?24:23;
    const auto sig=(static_cast<uint64_t>(x.sig)<<shift)/y.sig;
    return Normalize(sign,sig,x.exponent-y.exponent-shift);
}
float Fmac(float a,float b,float c) {
    const auto x=Decode(a),y=Decode(b),z=Decode(c);const auto sign=x.sign^y.sign;
    if(x.nan || y.nan || z.nan || (x.inf && !y.sig && !y.inf) || (y.inf && !x.sig && !x.inf))return Float(kNaN);
    if(x.inf || y.inf) {
        if(z.inf && z.sign!=sign)return Float(kNaN);
        return Float(sign|kInf);
    }
    if(z.inf)return Float(z.sign|kInf);
    const auto product=static_cast<uint64_t>(x.sig)*y.sig;
    if(!product)return Add(Float(sign),c);
    if(!z.sig)return Normalize(sign,product,x.exponent+y.exponent);
    const auto p=Term(product,x.exponent+y.exponent),q=Term(z.sig,z.exponent);
    if(sign==z.sign)return RoundWide(Sum(p,q),sign);
    const auto cmp=Compare(p,q);
    if(!cmp)return 0.0f;
    return cmp>0?RoundWide(Difference(p,q),sign):RoundWide(Difference(q,p),z.sign);
}
float Sqrt(float a) {
    const auto x=Decode(a);
    if(x.nan || (x.sign && (x.sig || x.inf)))return Float(kNaN);
    if(x.inf)return a;
    if(!x.sig)return Float(x.sign);
    const int inputE=x.exponent+23,e=inputE>=0?inputE/2:(inputE-1)/2;
    const unsigned shift=static_cast<unsigned>(x.exponent-2*e+46);
    const uint64_t squared=static_cast<uint64_t>(x.sig)<<shift;
    auto root=static_cast<uint64_t>(std::sqrt(static_cast<double>(squared)));
    while(root*root>squared)--root;
    while((root+1)*(root+1)<=squared)++root;
    return Normalize(0,root,e-23);
}
float FromInt(int32_t a) {
    const auto magnitude=a<0?static_cast<uint64_t>(-static_cast<int64_t>(a)):static_cast<uint64_t>(a);
    return Normalize(a<0?kSign:0,magnitude,0);
}

int ArcTan(float x) {
    const float pi2=Float(0x40c90fdau);
    const float z=std::fabs(x)>1.0f?Div(1.0f,x):x;
    float term=0.0f;
    for(int i=13;i>=1;--i) {
        float numerator=Mul(FromInt(i),FromInt(i));
        numerator=Mul(Mul(numerator,z),z);
        term=Div(numerator,Add(Add(FromInt(2*i),term),1.0f));
    }
    const float arc=Div(z,Add(1.0f,term));
    const float scaled=Add(Div(Mul(Add(arc,pi2),65536.0f),pi2),0.5f);
    const int angle=static_cast<int32_t>(scaled)&65535;
    return (x>1.0f?0x4000-angle:x< -1.0f?0xc000-angle:angle)&65535;
}
int ArcTan2(float y,float x) {
    if(y>x) {
        if(-y>x)return x==0.0f?0x8000:(ArcTan(Div(y,x))+0x8000)&65535;
        return y==0.0f?0x4000:(0x4000-ArcTan(Div(x,y)))&65535;
    }
    if(-y>x)return y==0.0f?0xc000:(0xc000-ArcTan(Div(x,y)))&65535;
    return x==0.0f?0:ArcTan(Div(y,x));
}
float Sin(int angle) {
    const float pi2=Float(0x40c90fdau),halfPi=Float(0x3fc90fdau);
    const float half=Div(Div(Mul(FromInt(angle&65535),pi2),65536.0f),2.0f);
    const int quadrant=static_cast<int32_t>(Add(Div(half,halfPi),0.5f));
    const float x=Sub(half,Mul(FromInt(quadrant),halfPi)),square=Mul(x,x);
    float term=0.0f;
    for(int i=11;i>=3;i-=2)term=Div(square,Sub(FromInt(i),term));
    const float tangent=Div(x,Sub(1.0f,term));
    const float result=Div(Mul(tangent,2.0f),Fmac(tangent,tangent,1.0f));
    return quadrant&1?-result:result;
}
float Cos(int angle) {
    int folded=angle&65535;
    if(folded>0x8000)folded=0x10000-folded;
    return Sin(0x4000-folded);
}
float Tan(int angle) {
    angle&=65535;
    if((angle&0x7fff)==0x4000)return Float(kMax);
    const float pi2=Float(0x40c90fdau),halfPi=Float(0x3fc90fdau);
    const float radians=Div(Mul(FromInt(angle),pi2),65536.0f);
    const int quadrant=static_cast<int32_t>(Add(Div(radians,halfPi),0.5f));
    const float x=Sub(radians,Mul(FromInt(quadrant),halfPi)),square=Mul(x,x);
    float term=0.0f;
    for(int i=13;i>=3;i-=2)term=Div(square,Sub(FromInt(i),term));
    const float tangent=Div(x,Sub(1.0f,term));
    if(!(quadrant&1))return tangent;
    return tangent==0.0f?Float(kMax):Div(-1.0f,tangent);
}
void ProjectionXY(float* x,float* y) {
    // Render_BeginWorld0x0C09A720 passes this integer FOV and aspect to
    // SDK0x0C0B6B90. Its half-angle0xE9E differs from the aiming focal's
    // 0xE9D; this distinction is original, not a renderer correction bias.
    const float vertical=Div(1.0f,Tan(0x1d3c>>1));
    if(x)*x=Div(vertical,Float(0x3faaaaaau));
    if(y)*y=vertical;
}
} // namespace game::naomi

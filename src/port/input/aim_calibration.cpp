#include "aim_calibration.h"
#include <algorithm>
#include <cmath>

namespace input {
bool AimCalibration::Apply(AimPoint p, AimPoint& out) const {
    double z=h[6]*p.x+h[7]*p.y+h[8];
    if (!std::isfinite(z) || std::abs(z)<1e-8) return false;
    out={(h[0]*p.x+h[1]*p.y+h[2])/z,(h[3]*p.x+h[4]*p.y+h[5])/z};
    return std::isfinite(out.x) && std::isfinite(out.y);
}
bool AimCalibration::Fit(const std::array<AimPoint,4>& s) {
    const std::array<AimPoint,4> target{{{.15,.15},{.85,.15},{.85,.85},{.15,.85}}};
    // Reject repeated, crossed or nearly collinear samples before solving the homography.
    double sign=0;
    for(int i=0;i<4;++i) {
        auto a=s[i],b=s[(i+1)%4],c=s[(i+2)%4];
        double cross=(b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x);
        if(!std::isfinite(cross) || std::abs(cross)<.002 || (sign && cross*sign<0)) return false;
        sign=cross;
    }
    double m[8][9]{};
    for(int i=0;i<4;++i) {
        double x=s[i].x,y=s[i].y,u=target[i].x,v=target[i].y;
        double a[9]{x,y,1,0,0,0,-u*x,-u*y,u},b[9]{0,0,0,x,y,1,-v*x,-v*y,v};
        std::copy(a,a+9,m[i*2]); std::copy(b,b+9,m[i*2+1]);
    }
    for(int col=0;col<8;++col) {
        int pivot=col;
        for(int r=col+1;r<8;++r) if(std::abs(m[r][col])>std::abs(m[pivot][col])) pivot=r;
        if(std::abs(m[pivot][col])<1e-9) return false;
        for(int j=0;j<9;++j) std::swap(m[col][j],m[pivot][j]);
        double v=m[col][col]; for(int j=col;j<9;++j) m[col][j]/=v;
        for(int r=0;r<8;++r) if(r!=col) {v=m[r][col];for(int j=col;j<9;++j)m[r][j]-=v*m[col][j];}
    }
    AimCalibration next;
    for(int i=0;i<8;++i) {next.h[i]=m[i][8];if(!std::isfinite(next.h[i]) || std::abs(next.h[i])>1000)return false;}
    next.calibrated=true;
    *this=next; return true;
}
} // namespace input

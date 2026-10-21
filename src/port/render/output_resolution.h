#pragma once
#include <algorithm>
#include <cmath>
namespace port {
enum class OutputResolution { Original=0, Match=1, Ssaa150=2, Ssaa200=3 };
struct RenderSize {int width=640,height=480;bool operator==(const RenderSize&)const=default;};
// imageWidth/Height are the visible image rectangle after aspect/integer fitting,
// not the monitor including pillarboxes. Budget scale is renderer-local only.
inline RenderSize ChooseOutputResolution(OutputResolution mode,int originalWidth,int originalHeight,
                                        int imageWidth,int imageHeight,float budgetScale=1.f) {
    if(originalWidth<1||originalHeight<1)return {};
    if(!std::isfinite(budgetScale))budgetScale=1;
    budgetScale=std::clamp(budgetScale,.25f,1.f);
    double width=originalWidth,height=originalHeight;
    if(mode!=OutputResolution::Original&&imageWidth>0&&imageHeight>0) {
        const double factor=mode==OutputResolution::Ssaa150?1.5:mode==OutputResolution::Ssaa200?2.:1.;
        width=imageWidth*factor;height=imageHeight*factor;
    }
    width*=budgetScale;height*=budgetScale;
    const double limit=std::min(1.,8192./std::max(width,height));
    return {std::max(1,static_cast<int>(std::lround(width*limit))),std::max(1,static_cast<int>(std::lround(height*limit)))};
}
} // namespace port

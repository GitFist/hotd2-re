#include "platform/platform.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
static void Check(bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){
    platform::Platform platform;platform::Options o;o.input=platform::Input::Mouse;o.guns=true;o.hidden=true;o.width=640;o.height=480;
    std::string error;Check(platform.Init(o,&error),error.c_str());
    auto* source=platform.Guns();Check(source!=nullptr,"window gun source created");
    platform.SetGameImage({80,0,480,480});
    Check(platform.SetInput(platform::Input::RawInput,&error),error.c_str());
    Check(platform.Guns()==source&&platform.CurrentInput()==platform::Input::RawInput,"runtime switch preserves wrapper pointer");
    auto& guns=platform.Lightguns();guns.Feed("1111111111111111",.3125,.4,true,false,1,0);
    Check(source->Player(0).connected&&std::abs(source->Player(0).x-.25)<.001,"raw aim is mapped exactly once");
    Check(platform.SetInput(platform::Input::Mouse,&error),error.c_str());
    Check(platform.Guns()==source&&!source->Player(0).connected,"mouse mode clears raw device buttons");
    Check(platform.SetInput(platform::Input::RawInput,&error),error.c_str());
    guns.Feed("1111111111111111",.5,.5,true,false,1,0);Check(source->Player(0).connected,"second raw registration works");
    Check(!platform.SetInput(platform::Input::Touch,&error)&&platform.CurrentInput()==platform::Input::RawInput,"unsupported switch leaves active input intact");
    puts("PASS hidden-window mouse/light-gun switching, stable source, image mapping and held-input clearing");
}

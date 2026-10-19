#pragma once
#include "display_menu.h"
#include "input/lightguns.h"

namespace play {
class LightgunMenu {
public:
    enum class Action { None, Save, MouseMode, GunMode };
    void Open(){open_=true;testing_=false;player_=-1;cursor_=0;calibration_=binding_=-1;pointer_={};notice_.clear();}
    void Close(input::Lightguns& guns){open_=false;guns.CancelAssign();guns.ReleaseButtons();}
    bool IsOpen()const{return open_;}
    bool Capturing()const{return calibration_>=0||binding_>=0;}
    Action Update(const MenuInput& in,input::Lightguns& guns,bool gunMode);
    void Sprites(const game::ExeImage& exe,const input::Lightguns& guns,bool gunMode,std::vector<game::SpriteDef>& out)const;
    void Notice(const std::string& text){notice_=text;}
    int Cursor()const{return cursor_;}
    int Player()const{return player_;}
    int CalibrationTarget()const{return calibration_;}
    bool Testing()const{return testing_;}
    int TestShots(int player)const{return shots_[player];}
    static float RowY(int row){return 88.f+27.f*row;}
private:
    bool open_=false,waitRelease_=false,testing_=false;
    int player_=-1,cursor_=0,calibration_=-1,binding_=-1;
    uint64_t captureAfter_=0;
    std::array<input::AimPoint,4> samples_{};
    std::array<int,2> shots_{};
    game::MenuPointerFocus pointer_;
    std::string notice_;
};
}

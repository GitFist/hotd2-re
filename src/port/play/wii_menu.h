#pragma once
#include "input/wii_remote.h"
#include "play/display_menu.h"

namespace play {
// Local configuration only: it never enters the deterministic game state or net packets.
class WiiMenu {
public:
    void Open(){open_=true;player_=-1;cursor_=0;calibration_=-1;pointer_={};notice_.clear();}
    bool IsOpen()const{return open_;}
    void Close(){open_=false;calibration_=-1;}
    bool Update(const MenuInput& in,input::WiiRemotes& remotes);
    void Sprites(const game::ExeImage& exe,const input::WiiRemotes& remotes,std::vector<game::SpriteDef>& out)const;
    void ModernHud(const input::WiiRemotes& remotes,game::WorldHud& hud)const;
    int Cursor()const{return cursor_;}
    int SelectedPlayer()const{return player_;}
    int CalibrationStep()const{return calibration_;}
    static float RowY(int row){return 88.0f+27.0f*row;}
private:
    bool open_=false,waitRelease_=false;
    int player_=-1,cursor_=0,calibration_=-1;
    game::MenuPointerFocus pointer_;
    std::array<input::wii::Point,4> samples_;
    std::string notice_;
};
} // namespace play

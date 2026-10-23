#pragma once

#include <cstdint>
#include <string>

#include "game/menu_pointer.h"
#include "game/world.h"
#include "play/display_menu.h"

namespace play {

// Local presentation only. A completed run is queued while the original result
// and initials screens finish; showing this overlay never mutates World state.
class RunNamePrompt {
public:
    enum class Action { None, Publish, Skip };

    bool Queue(uint64_t token, int table, uint32_t score, uint16_t rounds,
               const std::string& initialName = {});
    static bool Ready(const game::WorldHud& hud);
    bool MaybeOpen(const game::WorldHud& hud, bool blocked = false);
    bool IsOpen() const { return open_; }
    uint64_t Token() const { return token_; }
    const std::string& Name() const { return name_; }
    void Edit(const std::string& text, bool backspace = false);
    Action Update(const MenuInput& in);
    void BeginSubmitting();
    void FinishSubmission(bool accepted, const std::string& error = {});
    void Reset();
    void ModernHud(game::WorldHud& hud) const;

private:
    uint64_t newestToken_ = 0, token_ = 0;
    int table_ = 0, cursor_ = 0;
    uint32_t score_ = 0;
    uint16_t rounds_ = 0;
    bool pending_ = false, open_ = false, submitting_ = false, accepted_ = false, attempted_ = false;
    bool pointerVisible_ = false, typedFocus_ = false;
    float pointerX_ = 0, pointerY_ = 0;
    std::string name_, error_;
    game::MenuPointerFocus pointer_;
};

}  // namespace play

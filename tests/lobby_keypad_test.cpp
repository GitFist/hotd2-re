#include "port/play/lobby_keypad.h"

#include <cstdio>
#include <cstdlib>

namespace {
void Require(bool ok, const char* why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(1);
    }
}
}

int main() {
    for (int key = 0; key < 12; ++key) {
        const auto idle = play::UpdateLobbyKeypad(key, 0);
        Require(idle.key == key && !idle.digit && !idle.erase && !idle.back,
                "idle input preserves focus without activating a key");
        const uint32_t confirms[] = {4, 8};
        for (uint32_t confirm : confirms) {
            const auto action = play::UpdateLobbyKeypad(key, confirm);
            Require(action.key == key, "confirmation preserves focus");
            Require(action.digit == (key < 10 ? '0' + (key + 1) % 10 : 0) &&
                        action.erase == (key == 10) && action.back == (key == 11),
                    "both menu confirm bits activate each visible digit or action");
        }
    }
    Require(play::UpdateLobbyKeypad(0, 0x40).key == 4 && play::UpdateLobbyKeypad(4, 0x80).key == 0 &&
                play::UpdateLobbyKeypad(5, 0x40).key == 9 && play::UpdateLobbyKeypad(9, 0x80).key == 5,
            "horizontal navigation wraps within a digit row");
    Require(play::UpdateLobbyKeypad(10, 0x40).key == 11 && play::UpdateLobbyKeypad(11, 0x80).key == 10,
            "Delete and Back share the bottom row");
    Require(play::UpdateLobbyKeypad(0, 0x20).key == 5 && play::UpdateLobbyKeypad(9, 0x10).key == 4,
            "vertical navigation preserves the digit column");
    Require(play::UpdateLobbyKeypad(7, 0x20).key == 10 && play::UpdateLobbyKeypad(8, 0x20).key == 11 &&
                play::UpdateLobbyKeypad(10, 0x10).key == 6 && play::UpdateLobbyKeypad(11, 0x10).key == 9,
            "bottom-row navigation follows the visible keypad geometry");
    Require(play::UpdateLobbyKeypad(0, 0x10).key == 10 && play::UpdateLobbyKeypad(4, 0x10).key == 11 &&
                play::UpdateLobbyKeypad(10, 0x20).key == 1 && play::UpdateLobbyKeypad(11, 0x20).key == 4,
            "vertical navigation wraps between the top and bottom rows");
    const auto moveAndChoose = play::UpdateLobbyKeypad(0, 0x80 | 8);
    Require(moveAndChoose.key == 1 && moveAndChoose.digit == '2', "movement precedes confirmation");
    Require(play::UpdateLobbyKeypad(6, 0xF0).key == 6, "opposing directions cancel");
    Require(play::UpdateLobbyKeypad(-1, 0).key == 0 && play::UpdateLobbyKeypad(12, 0).key == 0,
            "invalid initial focus recovers to the first key");
    const auto backButton = play::UpdateLobbyKeypad(3, 2);
    Require(!backButton.digit && !backButton.erase && !backButton.back,
            "physical Back remains with the caller's existing delete-or-leave handling");

    int key = 0;
    const uint32_t inputs[] = {8, 0x80 | 8, 0x80 | 8, 0x80 | 8, 0x80 | 8};
    char code[6] = {};
    int digits = 0;
    for (uint32_t input : inputs) {
        const auto action = play::UpdateLobbyKeypad(key, input);
        key = action.key;
        if (action.digit) code[digits++] = action.digit;
    }
    Require(digits == 5 && code[0] == '1' && code[1] == '2' && code[2] == '3' && code[3] == '4' && code[4] == '5',
            "a five-digit room code can be entered with only controller navigation and confirm");
    std::puts("lobby_keypad_test: OK");
}

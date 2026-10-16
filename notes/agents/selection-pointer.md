# Remaining menu pointer controls (2026-10-15, build P)

Build `2026 10 15 P` / `2026101503` extends O's options and N's main-menu fixes to the remaining selection screens.
Both network clients need this build. No lobby-server or firewall change is required.

- Original: aim selects stock rows or the current player's slots; shoot stock to equip, an occupied slot to return
  its item, END to ready that player, arrows to scroll, BACK to leave. Existing quantity and compatibility checks
  still run. Two players keep separate slots and must each confirm. Full slots cannot be overwritten by a stock shot.
  Simultaneous purchases resolve the items actually aimed at; taking the last copy cannot shift the other player's
  shot onto a different item or duplicate that copy.
- Boss / Training / Scene Select: shoot side arrows/cards to browse, centre card or START to launch, BACK to leave.
  Boss and Training also have LEVEL arrows, retaining the existing unlock limits. Network choices remain P1-only.
- Training instructions: START / BACK. Failure: aim/click the existing Retry / Back choices. Passing results and
  high-score name entry already supported shooting and retain their original controls.
- Rankings: shoot the neighbouring page label/arrow, or the new BACK button.
- Shared room and pre-connection lobby rows use visible glyph bounds. Stationary aim preserves keyboard focus;
  clicking restores pointer focus. Blank shots cannot activate an old row. Private/P2P codes have ten shootable
  digits, DELETE and BACK; waiting/connecting/rejoining pages have clickable close/cancel controls.
- Horde's store, chapter results, pause/confirmation and Options/Display already supported pointer controls.

`world_menu_pointer.h` shares selection geometry and original handlers' button translation. Consume both Trigger
and synthetic Start on device shots. Note that the original mask tables name these oppositely on some screens:
test a player's entire 16-bit mask when deciding who can interact. Added labels use original menu-font assets;
explicitly request their texture files on selection screens. Sights use the original sprites above the near plane.
Port-only additions are gated by `portFeatures`; legacy captured menu execution remains unchanged.

Validation:

- `selection_pointer_test game`: stock equip/remove/scroll, compatibility preserving slot 0, individual ENDs,
  player ownership, blank shots, Boss browsing/start/difficulty, Training instructions/results, Scene chapter launch,
  ranking page changes and Back, and host-only multiplayer selection / returning to the shared lobby.
- `network_room_test game`: all mode lifecycles, both players, keyboard controls and 1,300 replay frames pass.
- Original capture comparison through frame 1250: 176 selection sprite frames and 13,531 draw matrices match.
- Boss 0 through frame 1000: 158 selection sprite frames and 4,124 draw matrices match.
- Training 0 through frame 1200: 348 selection sprite frames and 9,748 draw matrices match.
- Rankings through frame 3300: all 2,410 ranking sprite frames and 9,928 draw matrices match.
- `python tools/menu_pointer_smoke.py`: real hidden clients with separate runtime folders and a temporary local
  relay, mouse-created P2P room, all five digits entered by pointer, both players choose Original items and END,
  and both reach frame 2200 with hash `9A4D766C`. Output: `%TEMP%/hotd2_menu_pointer_n9r0k0v7`.
- Rendered Original, Boss, Training, Scene Select, rankings, lobby and keypad pages inspected at 640x480 in
  `build/menus-pointer-p/`. No user's running game was terminated. Physical light guns were not available.

Packages and installed-executable hashes: `build/dist-menus-p-final-20261015/verification.json`.
Small update preserves saves, settings and optional graphics assets; complete ZIP is for a fresh installation.

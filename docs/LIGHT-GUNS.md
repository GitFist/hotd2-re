# Light-gun setup

Open **Options > Light Guns** or **Pause > Light Guns**. Input changes take effect immediately and are saved automatically.

## One or two guns

1. Connect the guns and complete their manufacturer's tracking setup. Configure them to send mouse coordinates and mouse buttons. For Sinden guns, keep the Sinden software and tracking border running.
2. Set **Input mode** to **LIGHT GUNS**.
3. Select **Assign Player 1**, then pull that gun's trigger. Only use the intended gun while this screen is waiting.
4. For local co-op, select **Assign Player 2** and pull the second gun's trigger.
5. Open each player's settings to calibrate, change buttons, or select **Test both guns**. The test page shows separate crosshairs and shot counters. Press **Esc** to leave the test.

Assignments survive restarting the game and reconnecting a gun. A disconnected player's slot stays reserved. Replacing a gun or changing how Windows identifies it may require assigning it again. **Swap players** swaps the guns and their complete settings. **Clear assignments** releases both slots for setup again.

The mouse and keyboard remain available in setup. **Esc** cancels assignment, button capture, or calibration. Select **MOUSE** input mode to return to ordinary mouse/touchpad controls. Selecting either input mode disables native Wii Remote input so a Wii Remote cannot occupy an assigned gun slot; it can be enabled again from **Wii Remotes**.

Online play uses local **Player 1** input on each PC, even when joining the host as Player 2. Configure the gun you use online in the Player 1 slot.

## Buttons and aim

Defaults are left mouse button for **Fire**, right for **Reload**, button 4 for **Confirm**, and middle for **Pause**. Click an action and press the desired physical gun button to remap it. Mouse buttons 1 through 5 are supported. Assigning an already-used button swaps the two actions. Keyboard **Enter** and **Esc** remain available for confirmation and pause/back.

For **Calibrate aim**, stand at your normal playing position and shoot the four targets. Each target needs a fresh trigger press. Cancelling or an invalid calibration preserves the previous settings. **Reset calibration** restores the default aim mapping. Recalibrate after changing the display layout or the gun's tracking configuration.

The game saves input mode in `hotd2re_settings.ini` and gun assignments, mappings and calibration in `hotd2re_lightguns.ini`. Use a writable game folder. Device identifiers are kept locally as opaque values; these settings are excluded from releases.

## Compatibility

The game accepts standard Windows mouse/Raw Input devices. Two independent guns require separate device reports with absolute aim coordinates. Software that combines both guns into a single Windows cursor cannot provide independent two-player aim. A single software-mapped gun can use **MOUSE** mode instead.

These controls do not replace the manufacturer's tracking software, sensor setup, or firmware. Native recoil outputs and vendor-specific configuration are not included. Automated tests cover the setup and input paths; specific light-gun models still need physical testing.

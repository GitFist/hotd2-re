# Native Wii Remote controls

Open **Options > Wii Remotes** or **Pause > Wii Remotes**. Windows reads the remote directly; no mouse-mapping utility is needed.

## Connect

- **Bluetooth adapter:** turn on a powered sensor bar near the display, select **Connect remotes**, and press the remote's red **SYNC** button. The **1 + 2** guest connection is also supported. The search lasts 20 seconds. Select **Automatic reconnect** to keep searching while support is enabled.
- **Mayflash DolphinBar:** set the bar to **Mode 4**, sync the bar and remote, and select **Connect remotes** in the game. The bar handles its Bluetooth connection.
- Close other applications that are using the same Wii Remote. The game uses Windows HID/Bluetooth drivers; it does not replace Windows drivers, install custom drivers, or use Bluetooth passthrough.

Connected remotes are assigned Player 1 and Player 2, with matching player LEDs. **Swap players** exchanges their assignments and profiles. A lost connection keeps its slot reserved until it reconnects or is explicitly disconnected in setup. Online play uses the first local input slot for the local player, including the guest.

## Configure and calibrate

Select **Player 1 setup** or **Player 2 setup**. Defaults are **B: fire**, **A: reload**, **+: confirm**, and **HOME: pause**. Use the D-pad, keyboard, or mouse to navigate. Click a binding or use left/right to change it; duplicate bindings swap instead of assigning two actions to one button.

Select **Calibrate aim**, stand at your normal playing position, and aim at each of the four crosses. Press **B** once at each cross. Keep both ends of the sensor bar visible. **Esc** cancels without replacing your previous calibration. Calibrate again after moving the bar, changing display size, or changing playing position. Aim smoothing can be Off, Light, or Strong. The setup screen shows tracking, battery level, button presses, and the live aim position.

Preferences are saved beside the game in `hotd2re_wii.ini`. No Bluetooth addresses or device paths are saved or included in packages. Turn off **Wii Remote support** to release the connections and use the regular inputs.

## Compatibility and validation

The Windows backend targets Nintendo RVL-CNT-01 and RVL-CNT-01-TR HID devices, including the interfaces presented by DolphinBar Mode 4. Bluetooth adapter and third-party remote compatibility can vary. Native pointing requires an infrared sensor bar. MotionPlus, extension controllers, speaker audio and Bluetooth passthrough are not used.

Automated tests cover HID reports, malformed packets, four-point calibration, button mappings, short taps, disconnect/reconnect slot retention, mixed mouse/remote input, setup menus and saved preferences. Physical pairing and aiming still require real-hardware validation; no Wii Remote was available during development.

Protocol reference: [Wii Remote reports](https://wiibrew.org/wiki/Wiimote). Windows connections use [BluetoothSetServiceState](https://learn.microsoft.com/en-us/windows/win32/api/bluetoothapis/nf-bluetoothapis-bluetoothsetservicestate) and overlapped HID I/O. The implementation is original project code; no Dolphin code is included.

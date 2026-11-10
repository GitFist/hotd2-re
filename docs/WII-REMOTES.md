# Native Wii Remote input

HOTD2RE supports two separate Wii Remote connection paths.

- **Windows Bluetooth HID (default):** ordinary Windows Bluetooth pairing and DolphinBar Mode 4. This path does not require the passthrough DLL or libusbK.
- **USB Bluetooth passthrough (optional):** selected explicitly in Options > Wii Remotes. The game loads `wii_bt_passthrough.dll` from the same directory as `hotd2.exe` / `play.exe`. It also requires the matching-architecture `libusb-1.0.dll` in that directory; both are shipped together as optional runtime files. The host DLL owns the selected USB HCI adapter through libusb. On Windows, the device must use a driver supported by libusb, such as WinUSB or libusbK.

If the DLL is missing, its API version is incompatible, or no claimable USB Bluetooth HCI adapter is available, HOTD2RE falls back to Windows Bluetooth HID and displays the reason in the Wii Remote status page. The game input library itself does not link libusb and does not statically include the USB transport.

Both runtime DLLs must match the game executable architecture: x86 for a 32-bit executable or x64 for a 64-bit executable. With passthrough OFF, neither DLL is loaded and the selected USB adapter is not claimed by HOTD2RE.

## Adapter selection

The passthrough runtime is **not tied to the CSR8510**. It looks for a standard USB Bluetooth HCI interface with the required Interrupt IN and Bulk IN/OUT endpoints, then verifies that libusb can open and claim it. The CSR8510 A10 (`0A12:0001`) is simply the first adapter used for physical testing.

Selection is saved in `hotd2re_wii.ini`:

- `usb_vid=-1` and `usb_pid=-1`: Automatic; use the first claimable standard HCI adapter.
- Set **both** values to a USB Vendor ID and Product ID to select a specific device. Decimal and hexadecimal values are accepted.
- A partial pair returns safely to Automatic.
- An explicitly selected adapter that is unavailable or cannot be claimed does **not** silently fall back to another device.

For the CSR8510 used in the first hardware tests, the explicit values are `usb_vid=0x0A12` and `usb_pid=0x0001`. This is an example, not a requirement.

Run `wii_bluetooth_probe.exe` without arguments to inspect all USB devices and report claimable HCI adapters. To check a particular adapter, pass both IDs: `wii_bluetooth_probe.exe 0x0A12 0x0001`.

On Windows, the selected adapter must be accessible through a libusb-supported driver such as WinUSB or libusbK. Merely appearing in Windows Bluetooth settings does not guarantee passthrough compatibility. Installing a libusb driver on an adapter can make it unavailable to the normal Windows Bluetooth stack; use a dedicated USB adapter when possible. Do not change the driver of your normal Windows radio unless you intend to take it away from Windows Bluetooth.

## Connect

1. Enable Wii Remote support in **Options > Wii Remotes** (or **Pause > Wii Remotes**).
2. Leave **USB Bluetooth passthrough OFF** for ordinary Windows Bluetooth / DolphinBar use, or turn it ON for the compatible USB HCI adapter.
3. Select **Connect remotes**.
4. Press the remote's red **SYNC** button during the search.

The direct USB path performs Bluetooth Classic inquiry and recognizes the standard Nintendo Wii Remote Class of Device values (using remote-name validation as a fallback for other devices). It uses the proven R1 / unknown-clock-offset HCI Create Connection parameters and accepts a remote-initiated Connection Request if it races with the host request. Initial pairing follows the legacy SYNC host-address PIN exchange, then enables link encryption and opens HID L2CAP/HIDP. QoS and Sniff policy changes are deliberately kept out of the initial pairing sequence. It currently supports up to two remotes. The 1+2 guest-pairing path remains available through Windows Bluetooth HID.

## DLL API and transport

The game loads the DLL dynamically and resolves a versioned C API. API v2 accepts the configured USB VID/PID pair at backend creation; `-1/-1` requests automatic selection. The boundary uses fixed-width C structures only; no C++ STL objects or CRT-allocated memory cross the DLL boundary. The DLL owns its libusb context, USB HCI interface and Bluetooth worker thread.

The DLL uses HCI commands on the USB control pipe, HCI events on the USB interrupt IN endpoint, ACL data on USB bulk IN/OUT endpoints, HID Control PSM `0x0011`, HID Interrupt PSM `0x0013`, and continuous report `0x33` for core buttons, accelerometer and extended IR. A powered infrared sensor bar is required for pointing.

## Pairing keys

When the remote sends `HCI Link Key Notification`, the passthrough runtime saves the key locally and restores known keys after HCI Reset. The file is:

```
%LOCALAPPDATA%\\HOTD2RE\\wii_link_keys.txt
```

It contains Bluetooth addresses and link keys in hexadecimal. Treat it as a credential: do not publish it or include it in bug reports. It is separate from `hotd2re_wii.ini`, which stores player bindings, calibration and the passthrough selection.

## Build and validation

Configure Windows with `-DHOTD2_WII_USB_PASSTHROUGH=ON` to build the optional DLL and low-level adapter probes. Build targets:

```powershell
cmake --build build --config Release --target hotd2 play wii_remote_test wii_bt_passthrough wii_bt_passthrough_api_test wii_bluetooth_probe wii_hci_probe
```

The passthrough DLL and its `libusb-1.0.dll` dependency are emitted alongside the game executables. Build Win32 for x86 executables/DLLs or x64 with `-DHOTD2_ALLOW_64BIT=ON`. Packaging admits the pair only when both PE architectures match and includes libusb's `COPYING` license. The API smoke test loads the DLL and checks its exports, version, disabled lifecycle and snapshot ABI without requiring a Bluetooth adapter or Wii Remote. This is not physical hardware validation.

The standalone diagnostic project `WIimoteDLL_Libusbk` is the protocol reference for the proven discovery/pairing sequence. The long-term direction is to make that repository the reusable, application-independent host library and have HOTD2RE consume it through a stable API. The current in-game implementation has only been physically verified with the CSR8510 and one Wii Remote; other adapters still need testing.

## Scope

The passthrough backend targets standard USB Bluetooth HCI adapters and Nintendo RVL-CNT-01 / RVL-CNT-01-TR remotes. MotionPlus, Nunchuk/Classic Controller extensions, speaker audio, Balance Board and vendor-specific HID extensions are not implemented. Windows Bluetooth HID remains available without the optional DLL.

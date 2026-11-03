# Native feedback

Open **Options > Devices > Feedback** (also available while paused). Feedback is off by default. Select a local player, enable controller rumble or choose a light-gun family, then use **Test shot** and **Test damage**.

| Device | Setup | Feedback |
| --- | --- | --- |
| Xbox / PlayStation controller | Set Controller rumble above Off | Shot vibration and damage rumble, when supported by the connected controller |
| OpenFIRE | Select its COM port | Recoil and damage rumble |
| GUN4IR | Select its COM port | Recoil and damage rumble |
| Blamcon | Select its COM port | Recoil and damage rumble, if fitted |
| Sinden | Select driver gun A or B; enable recoil and start the Recoil Server in the Sinden app | Single-shot recoil |

The serial adapters send commands directly from the game. Sinden uses its driver's direct TCP API on localhost port 13000. MAMEHooker is not required. Keep the normal tracking setup running where required. Close other programs using the same feedback port before enabling this feature.

Match each player's feedback port to the gun assigned under **Devices > Light Guns**. The feedback connection is selected separately from aiming. Two local players need distinct COM ports, or distinct Sinden driver guns. Online play uses **local Player 1** feedback on each PC, including the player who joins as Player 2.

Shot feedback follows accepted gameplay shots, including special weapons. Dry fire, reloads, menu clicks and attract demos do not create game recoil. Damage feedback follows accepted hits. The game stops its effects on pause, focus loss, replay/catch-up, detected desync and exit. Serial handoff is released when inactive, restoring the gun's own trigger behavior. Controller effects are finite; gun pulse timing remains controlled by its firmware settings.

Preferences save in `hotd2re_feedback.ini`. For a Sinden server using a different port, change `player1.sinden_port` or `player2.sinden_port` in that file while the game is closed. The in-game status reports connection/dispatch state; Sinden's API has no device acknowledgement.

Automated tests use virtual controllers, mock serial transports and a local TCP test server. Physical light-gun recoil and Sinden driver interoperability still require testing. The Sinden adapter sends one documented command per TCP connection because the vendor documentation does not specify stream framing.

Protocol references: [OpenFIRE](https://github.com/TeamOpenFIRE/OpenFIRE-Firmware/wiki/MAMEHOOKER-Documentation), JB Game Lab GUN4IR Manual v1.26 pp. 20–21, [Blamcon](https://blamcon.com/get-started-with-blamcon/serial-commands/), and `RecoilTcpServerReadme.txt` in the [official Sinden Windows driver](https://sindenlightgun.com/drivers/).

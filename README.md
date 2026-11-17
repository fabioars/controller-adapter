# Controller adapter for PS3 and Xbox 360 (ESP32-S3)

Use an **Xbox Series X|S Wireless Controller** on a **PS3** or an **Xbox 360**. The ESP32-S3 connects to the controller over Bluetooth LE and plugs into the console by USB, where it acts as the console's own wired controller:

| Console  | The adapter acts as                                                                                 |
| -------- | --------------------------------------------------------------------------------------------------- |
| PS3      | a wired **DualShock 3** (Sony `054C:0268`)                                                          |
| Xbox 360 | a wired **Xbox 360 controller** (Microsoft `045E:028E`), including the console's security handshake |

The console is detected automatically, or you can choose it on the configuration page.

```
Xbox Series controller ))) BLE ))) ESP32-S3 ── USB cable ── PS3 or Xbox 360
                                    (Bluepad32)  (TinyUSB: DS3 or X360)
```

## Supported devices

✅ supported · 🧪 experimental · ❌ not supported. Nothing has been tested on real hardware yet; the firmware builds and the report encoding is unit tested.

### Controllers (connect to the adapter)

| Controller                                     | Connection                         | Status | Notes                                       |
| ---------------------------------------------- | ---------------------------------- | ------ | ------------------------------------------- |
| Xbox Series X\|S Wireless Controller           | Bluetooth LE                       | ✅     | The target controller                       |
| Xbox One S / Elite Series 2 (current firmware) | Bluetooth LE                       | 🧪     | Same driver as Series X\|S; untested        |
| Xbox One (original, no Bluetooth)              | —                                  | ❌     | No Bluetooth                                |
| DualShock 4                                    | Classic Bluetooth                  | ❌     | The ESP32-S3 has no Classic Bluetooth       |
| DualSense                                      | Classic Bluetooth                  | ❌     | Same                                        |
| 8BitDo controllers                             | Classic Bluetooth / 2.4 GHz dongle | ❌     | Same; the dongle would need a USB host port |
| Xbox 360 controller (wireless or wired)        | Microsoft 2.4 GHz / USB            | ❌     | Not Bluetooth; would need a USB host port   |

### Consoles (the adapter plugs into)

| Console                       | Acts as                                | Players per adapter | Status | Notes                                                                                                    |
| ----------------------------- | -------------------------------------- | ------------------- | ------ | -------------------------------------------------------------------------------------------------------- |
| PS3                           | Wired DualShock 3                      | 1                   | ✅     | PS button, analog triggers, rumble                                                                       |
| PS3                           | Several DualShock 3s on one USB device | 2–4                 | 🧪     | The PS3 may not accept it; use one adapter per player instead                                            |
| Xbox 360                      | Wired Xbox 360 controller              | 1                   | ✅     | Passes the console's security check via libxsm3; Guide button, rumble                                    |
| PS4 / PS5 / Xbox One / Series | —                                      | —                   | ❌     | Need security chips or keys the adapter doesn't have                                                     |
| PC                            | DS3 or Xbox 360 controller             | 1                   | 🧪     | Not a goal. Windows and Linux support the Xbox 360 mode out of the box; Linux also supports the DS3 mode |

### Boards (run the adapter firmware)

| Board                                      | Status | Notes                                              |
| ------------------------------------------ | ------ | -------------------------------------------------- |
| ESP32-S3 N16R8 (DevKitC-1 style, 2× USB-C) | ✅     | Recommended: one port to flash, one to the console |
| ESP32-S3 N16R8, single USB-C               | ✅     | Hold BOOT to re-flash; no serial log               |
| Other ESP32-S3 boards                      | ✅     | Change the LED pin in `make menuconfig` if needed  |
| ESP32 (original, e.g. "CH9102X" boards)    | ❌     | No native USB device                               |
| ESP32-C3 / C6 / H2                         | ❌     | Can't act as a USB controller                      |
| ESP8266                                    | ❌     | No Bluetooth, no native USB                        |

## Hardware

Built for the common **ESP32-S3 N16R8** boards (16 MB flash, 8 MB PSRAM, USB-C), sold as "ESP32-S3 DevKitC-1 N16R8". Other ESP32-S3 boards work too.

- **Two USB-C ports (recommended):** **COM/UART** goes to the computer (flashing, logs) and **USB** goes to the console.
- **One USB-C port:** works, but once the adapter firmware runs that port _is_ the console controller. To flash again, hold **BOOT** while plugging it into the computer, then run `make deploy`. There are no logs on this board type.
- **Status LED** (the board's RGB LED, GPIO48; change it in `make menuconfig` if yours uses 38):

  | LED            | Meaning                                         |
  | -------------- | ----------------------------------------------- |
  | blue, blinking | searching for a controller                      |
  | yellow         | controller connected, console not connected yet |
  | green          | controller and console connected: ready to play |
  | dim red        | idle                                            |

- See [Supported devices](#supported-devices) for other boards and controllers.

## Build and deploy

```sh
make setup     # installs ESP-IDF 5.5.1 into ~/esp/esp-idf if missing, fetches Bluepad32 + libxsm3
make deploy    # build + flash + serial monitor (Ctrl+] to quit)
```

Connect the board's **UART** port to your computer for flashing. The ESP32-S3 is auto-detected: every USB serial port is probed and the one with an ESP32-S3 is used (other boards plugged in at the same time are ignored). To skip the scan, pass `make deploy PORT=/dev/cu.usbserial-1410`. Run `make help` to see all targets (`build`, `flash`, `monitor`, `menuconfig`, `test`, `erase`, ...).

`make test` runs host unit tests for the report encoding and needs no board.

## Use

1. Plug the board's **USB** (native) port into the console. The console powers it.
2. First time: hold the **pair** button on top of the Xbox controller until the logo flashes fast. The controller buzzes once when the adapter accepts it.
3. After that, pressing the Xbox button reconnects it automatically.
4. Press the Xbox button (= PS / Guide button) to bring up the console menu or assign the controller.

By default one controller is accepted (see [Multiple controllers](#multiple-controllers-experimental-ps3-only)). Non-Xbox controllers are rejected.

## Console detection

On the page, **Console** can be **Auto** (default), **PS3** or **Xbox 360**. Changing it restarts the adapter.

In **Auto**, the adapter starts as the controller for the console it last detected (PS3 the very first time), then checks that the console behaves as expected:

- a **PS3** asks for DualShock 3 setup reports right after connecting;
- an **Xbox 360** sets the player LED and starts its security handshake.

If that doesn't happen (4 s for the PS3, 10 s for the Xbox 360, or 20 s with no connection at all), the adapter restarts once as the other console's controller. The detected console is remembered, so the next power-up goes straight to it. If auto-detection guesses wrong with your console, pick it manually.

## Configuration page (Wi-Fi)

The adapter creates a Wi-Fi network when it powers up:

- **Network:** `PS3-Xbox-Adapter`
- **Password:** `ps3adapter`
- **Page:** http://192.168.4.1

Change the network name and password with `make menuconfig` → _PS3 Xbox Adapter_. You can turn Wi-Fi off entirely there too.

On the page you can:

- see which console is connected (and, on Xbox 360, whether it authenticated the controller and which player it assigned)
- choose the **console** (Auto / PS3 / Xbox 360) and the number of **players**
- see each controller's connection and battery level
- **Pair new controller**, **Disconnect** a controller
- see the **saved controllers** and **forget** one or all of them
- watch each controller's **live input**, handy for checking the mapping
- **Turn off Wi-Fi**

Wi-Fi and Bluetooth share one radio, so a running Wi-Fi network can add input lag. Turn it off from the page before playing. It comes back the next time the adapter is powered up (when the console turns on).

## Multiple controllers (experimental, PS3 only)

On the PS3 one adapter can serve up to **4 players**. Choose the number under **Players** on the configuration page; the adapter restarts and the PS3 sees it re-plugged.

- With **1 player** (default) the adapter is exactly one DualShock 3.
- With **2–4 players** it appears as one USB device with one DualShock 3 interface per player. **The PS3 may not accept this**, since a real DS3 has only one. If controllers stop working, set it back to 1. With 4 players, rumble travels over a slower USB channel because the ESP32-S3 runs out of endpoints.
- Controllers fill player slots in the order they connect. Each one buzzes its player number (player 2 buzzes twice).
- When all slots are taken, **Pair new controller** disconnects the last player to make room. A disconnected controller can't rejoin for 30 s, so the new one gets the slot.
- Each player can be disconnected individually from the page.

On the **Xbox 360** it's always one player per adapter: a wired 360 controller is one USB device. For more players, use one adapter per player (also the reliable option on the PS3).

## Button mapping

| Xbox Series      | PS3                                | Xbox 360         |
| ---------------- | ---------------------------------- | ---------------- |
| A / B / X / Y    | Cross / Circle / Square / Triangle | A / B / X / Y    |
| LB / RB          | L1 / R1                            | LB / RB          |
| LT / RT (analog) | L2 / R2 (analog)                   | LT / RT (analog) |
| LS / RS click    | L3 / R3                            | LS / RS click    |
| View / Menu      | Select / Start                     | Back / Start     |
| Xbox button      | PS                                 | Guide            |
| D-pad, sticks    | D-pad, sticks                      | D-pad, sticks    |
| Share            | (unused)                           | (unused)         |

Rumble from games is forwarded to the controller on both consoles.

## Limitations

- **PS3 pressure-sensitive buttons:** face buttons, bumpers and D-pad report fully pressed or released. Only the triggers are analog.
- **PS3 motion (SIXAXIS):** the accelerometer and gyro always report "flat and still". Tilt-controlled games won't respond.
- **Xbox 360 headset / chatpad:** not supported (the audio and plug-in interfaces exist only because the console expects them).
- **Stick resolution:** sticks are passed through at 8 bits; fine for play, slightly coarser than a real 360 controller.
- **USB only:** the adapter doesn't connect to either console over Bluetooth or wireless.

## Code layout

| File                                      | Purpose                                                                                  |
| ----------------------------------------- | ---------------------------------------------------------------------------------------- |
| `main/xbox_bt.c`                          | Bluepad32 platform: Xbox-only filter, player slots, button mapping, rumble out           |
| `main/usb_out.c`                          | TinyUSB setup, descriptor callbacks, console auto-detection                              |
| `main/ds3_usb.c`                          | PS3 personality: DS3 descriptors, PS3 setup-request replies, report task                 |
| `main/x360_usb.c`                         | Xbox 360 personality: descriptors, XInput class driver, XSM3 authentication, report task |
| `main/ds3_report.c`, `main/x360_report.c` | Pure report encode/decode (unit tested)                                                  |
| `main/tusb/tusb_config.h`                 | TinyUSB configuration                                                                    |
| `main/pad_state.c`                        | Lock-protected input/rumble state shared between the BT and USB tasks                    |
| `main/bt_status.c`                        | Bluetooth status snapshot (connections, saved controllers) for the web page              |
| `main/adapter_config.c`                   | Saved settings: console, players, last detected console                                  |
| `main/web_config.c`                       | Wi-Fi access point + HTTP server + JSON API                                              |
| `main/web/index.html`                     | The configuration page (embedded in the firmware)                                        |
| `main/status_led.c`                       | Status on the board's RGB LED                                                            |
| `components/xsm3/`                        | Builds libxsm3 (fetched into `third_party/`) as a component                              |
| `partitions.csv`                          | Flash layout for 16 MB boards                                                            |
| `Makefile`                                | Setup, build, flash, monitor, test                                                       |

## Credits and licenses

- [Bluepad32](https://github.com/ricardoquesada/bluepad32) (Apache-2.0) for the Bluetooth controller support.
- DS3 and Xbox 360 descriptors and the PS3 setup-request replies follow [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE) (MIT).
- [libxsm3](https://github.com/InvoxiPlayGames/libxsm3) (**LGPL-2.1**) answers the Xbox 360's controller authentication. It's fetched separately and built as its own library; if you distribute firmware binaries, the LGPL's terms for libxsm3 apply (e.g. let people relink with a modified libxsm3). It's pinned to commit `7f1b78c` because newer commits contain a SHA-1 rewrite that doesn't compile.

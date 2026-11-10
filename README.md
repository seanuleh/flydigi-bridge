# flydigi-bridge

A custom 2.4 GHz receiver for **Flydigi Vader 4 / Vader 4 Pro** controllers: one nRF52840 board replaces the
per-controller USB dongles and shows up on the PC as standard **Xbox 360 (XInput)** controllers.

Why: the stock dongles drop out and fail to reconnect, which gets much worse with two controllers. The
bridge never sleeps, hunts for controllers on every channel, and serves several controllers from one radio.

> Unofficial, reverse-engineered, not affiliated with Flydigi.

## Status

| | |
|---|---|
| Wireless protocol | ✅ Fully decoded. Plaintext Nordic ESB at 2 Mbps; reports, handshake and acks documented |
| Dongle emulation | ✅ Controllers connect to the bridge without their official dongle |
| Two controllers on one bridge | ✅ Both stream at the same time (radio pipes 0 and 1) |
| USB output | ✅ Two Xbox 360 pads, tested on Windows 11 with `045e:028e`. Default build now uses `1209:0001` + MS OS 2.0 descriptors so Windows also gets the config COM port (**untested on Windows**) |
| Channel hunting | ✅ Finds searching controllers on any channel, and moves off a channel with heavy loss (controllers follow) |
| Learn mode, slots in flash | ✅ Adopts a controller's existing address in about a second |
| Web config page | ✅ [seanuleh.github.io/flydigi-bridge](https://seanuleh.github.io/flydigi-bridge/) (Chrome/Edge, Web Serial): live input, signal, rename, learn, player order |
| Full pairing (more than 2 controllers) | 📝 Planned: [docs/learn-and-pairing-plan.md](docs/learn-and-pairing-plan.md) |
| Rumble, 1000 Hz | ❌ Not yet. The downstream (dongle → controller) command path is still undecoded |
| Extra buttons (paddles, C/Z) | ❌ Received but not mapped yet |

Controller addresses live in the board's flash. Add yours with **Learn** on the web page (unplug the
controller's own dongle, then turn the controller on), or by hand with the `addr` console command. The
sniffer route still works too (see [Finding a controller's address](#finding-a-controllers-address)).

## Hardware

- Any **nRF52840** board with native USB and the Adafruit nRF52 UF2/serial bootloader. Developed on a
  Pro Micro nRF52840 (nice!nano clone), built as `adafruit:nrf52:feather52840`.
- Flydigi Vader 4 Pro controllers (device id 105, firmware 6.9.5.5), in 2.4 GHz dongle mode.

## Repository layout

```
firmware/
  fd_bridge/      the bridge: dongle emulation for 2 controllers, slots in flash, learn mode,
                  USB Xbox 360 interfaces + serial console
  fd_sniff/       sniffer: fixed-address or promiscuous ESB capture, controlled over serial
  fd_dongle/      single-controller dongle emulator (the first working prototype)
  legacy/         June 2026 sketches (promiscuous ESB / BLE scanners), kept for reference
tools/
  fd_host.py      drive fd_sniff / fd_bridge over serial and log to a file
  decode_esb.py   decode raw ESB dumps (9-bit packet header realignment + CRC check)
  find_addr.py    find recurring addresses in promiscuous captures
  fd_cmd.py       send Flydigi vendor commands through an official dongle (macOS, needs sudo)
  gc_dump.swift   print macOS GameController state (used to map buttons)
  correlate_capture.py / correlate.py   record radio + Xbox state together, map radio bits to buttons
  legacy/whad/    June 2026 scripts for WHAD/ButteRFly
web/              index.html: Web Serial config page (published to GitHub Pages from branch gh-pages)
docs/             status, protocol, firmware, tools, vendor commands, plans; CLAUDE.md indexes them
captures/         key radio recordings backing the protocol docs (see captures/README.md)
```

## Build and flash

Needs `arduino-cli` with the Adafruit nRF52 core:

```sh
arduino-cli core install adafruit:nrf52 --additional-urls https://adafruit.github.io/arduino-board-index/package_adafruit_index.json
cd firmware/fd_bridge
arduino-cli compile -b adafruit:nrf52:feather52840 --output-dir build .
arduino-cli upload  -b adafruit:nrf52:feather52840 -p /dev/cu.usbmodemXXXX --input-dir build .
```

The upload resets the board into its serial bootloader automatically. If that fails, double-tap reset.

On macOS, the first time a new USB identity appears you'll get an "Allow accessory to connect?" prompt.

## Using the bridge

1. Unplug the official dongles, so the controllers don't connect to them instead.
2. Plug in the bridge and turn the controllers on (dongle mode). They connect within a few seconds.
3. Debug console: `python3 tools/fd_host.py 10 /tmp/log.txt` prints status every 2 s. Serial commands are
   listed in [docs/firmware.md](docs/firmware.md).

## Finding a controller's address

Flash `firmware/fd_sniff`, turn the controller on **without** its dongle (it keeps calling out on its stored
address), then:

```sh
python3 tools/fd_host.py 20 cap.txt a d10 m1,aa,55,00000000,00000000,1   # promiscuous sweep
python3 tools/find_addr.py cap.txt                                     # most common 5-byte head = address
```

## Protocol summary

Details: [docs/protocol.md](docs/protocol.md). Current state and next steps: [docs/status.md](docs/status.md).

- **Radio:**
  - Nordic ESB at 2 Mbps.
  - 9-bit packet header (6-bit length, 2-bit packet ID, 1 flag bit).
  - CRC-16 (init `FFFF`, polynomial `1021`), computed over the address too.
  - A 5-byte address per controller, set when it paired with its dongle.
- **The controller transmits and the dongle acknowledges with a reply payload:**
  - 32-byte gamepad report every 10 ms → dongle replies `02 aa <report[30]+1> 00`, 192 µs after.
  - Searching (no dongle): 4-byte pings at about 2 kHz and a 21-byte message every 10 ms, all on one channel.
- **Report layout:**
  - byte 3: d-pad, A, B, Back, X
  - byte 4: Y, Start, LB, RB, LT/RT digital, stick clicks
  - bytes 24–27: sticks
  - bytes 28–29: triggers
  - bytes 7–19: gyro and accelerometer
- **nRF52 gotcha:** the radio sends address bits LSB-first, base bytes first and the prefix byte **last**.
  See `base_of()` in the firmware.

## Credits

- [uiltonlopes/flydigi-space-station-mac](https://github.com/uiltonlopes/flydigi-space-station-mac): Flydigi
  USB vendor protocol (Apex 4), report-rate command, button naming.
- [WHAD / ButteRFly](https://github.com/whad-team/butterfly): used for the first captures.

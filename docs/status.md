# Status and next steps

Last updated 2026-11-10.

## Working

- Protocol decoded ([protocol.md](protocol.md)).
- `fd_bridge` serves **two controllers at once** (red and blue) on macOS:
  - each one handshakes and streams about 100 reports a second;
  - channel hunting finds searching controllers;
  - the bridge appears as an Xbox 360 controller.
- **Windows 11 (HTPC), 2026-11-10:** both controllers work in a browser gamepad tester. Windows binds the
  whole device to its built-in Xbox 360 driver (`XnaComposite`, "Xbox 360 Controller for Windows") with
  two pads, `IG_00` and `IG_01`. No extra driver needed. Reliability, measured with
  `tools/xinput_watch.py`:
  - power-on orders: blue then red, both at once, and controllers on before the bridge all reconnect
    in about 14 s, each controller back on its own slot. When both start together the second is about
    4 s behind (one hunt at a time);
  - PC sleep (82 s) and wake: both pads live straight after wake, no re-enumeration;
  - idle on a long shielded USB cable, board in open air near the controllers: no drops, full
    ~100 reports a second per pad.
- **Placement matters.** Plugged straight into the HTPC's front-panel port, both controllers dropped
  while idle and re-handshook by themselves (blue about 5 times in 2 minutes), and even while connected
  often only about half the reports arrived. On the long cable the same firmware had no drops. Run the
  board on a USB extension, away from the PC case.

- **Slots in flash, learn mode, web page (2026-11-10, tested on macOS only):**
  - controller addresses, names and player order are stored in flash; the old hardcoded pair is the default;
  - player rule: with both on, Blue is player 1; a controller on its own is player 1;
  - learn mode re-learned blue in about 1 s;
  - `web/index.html` (Web Serial): slot cards with live input, signal and loss, rename, forget, learn,
    player choice. Renders in demo mode (`?demo`); **not yet used against the real bridge** (needs the
    port picker click).

- **Automatic channel moves (2026-11-10, Mac):** heavy loss on one channel (about 35 of 100 reports, both
  controllers alike) turned out to be the channel itself, not hardware. The bridge now leaves a bad channel by
  itself and the controllers follow without dropping (firmware.md, "Channel quality").

## Known issues and limits

- macOS shows only the first of the two Xbox interfaces, and none at all in the default (Windows composite)
  USB mode. Build with `USB_XBOX_IDS=1` to get a pad on the Mac.
- **No COM port on Windows.** Because VID/PID is 045E:028E, the Xbox composite driver claims the whole
  device instead of `usbccgp`, so the CDC debug interface never enumerates. Web Serial is out on Windows
  as things stand.
- Rumble, the extra buttons and the polling rate are not supported (the downstream path is unknown).
- CRC-bad counter around 40 a second with both controllers active (collisions; retransmits hide them).
- No hunting for a second controller while the first one is connected.
- Weak links drop instead of riding out losses: a few lost reports in a row and the controller times
  out and searches again. A more tolerant timeout would help weaker setups (not needed with the cable).
- One plug-in on Windows powered the board (controllers paired) but never enumerated; a replug fixed it.
  Probably a badly seated plug; not reproduced on the retry.
- Not tested: a 30-minute-plus session, wake after hours of sleep (the user chose to skip these).

## Next step: Windows test of the composite build

1. Flash `fd_bridge` (default build) on the HTPC: double-tap reset, copy the UF2 (firmware.md, "USB").
2. Check Device Manager: two Xbox 360 controllers **and** a COM port, no warning icons.
   If the pads don't get a driver, check which IDs `C:\Windows\INF\xusb22.inf` lists
   (`findstr /i "USB\\" C:\Windows\INF\xusb22.inf`) and try `XUSB20`; otherwise fall back to WebUSB.
3. Gamepad tester: both pads; player order Blue then Red; one controller alone is player 1;
   turning a controller off releases its buttons (neutral report).
4. Open `web/index.html` in Chrome or Edge (serve it from localhost, or publish it over https), connect,
   and try rename, learn and the player choice.

## Then: full pairing, to go past two controllers

Today the bridge holds **two controllers at most**. Learned controllers keep their dongle-given addresses,
and the radio only fits two unrelated addresses (pipes 0 and 1). With full pairing the bridge picks the
addresses itself (one shared base, distinct prefixes), so up to 7 fit on pipes 1–7.

1. Research session with the user and an official dongle: capture a real pairing (plan §5 phase A).
   Needs the controller's pairing button combo.
2. Decode and replay it from test firmware (§5 phase B).
3. Build `pair <slot>` into the bridge and the web page (§5 phase C).
4. Raise the slot count: `NSLOT` / `XI_MAX` (2 today), more Xbox interfaces, player rule for more than two
   pads, a wider slot table in flash (bump the config version).

Details: [learn-and-pairing-plan.md](learn-and-pairing-plan.md) §5, §6, §8.

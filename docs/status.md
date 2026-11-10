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
    player choice. Used against the real bridge on the Mac (2026-11-10).
  - Hosted at **https://seanuleh.github.io/flydigi-bridge/** (GitHub Pages, branch `gh-pages`, which holds only
    `index.html`). To republish after changing `web/index.html`: commit to main, then push a new `gh-pages`
    commit whose tree is just that file (`git hash-object -w` → `git mktree` → `git commit-tree -p origin/gh-pages`).
- **TX power +8 dBm** (the maximum; only makes our acks louder, not our hearing).

- **Automatic channel moves (2026-11-10, Mac):** heavy loss on one channel (about 35 of 100 reports, both
  controllers alike) turned out to be the channel itself, not hardware. The bridge now leaves a bad channel by
  itself and the controllers follow without dropping (firmware.md, "Channel quality").

- **Couch baseline (HTPC, 2026-11-10, chip antenna, 60 s on channel 5):** the living room is busy
  (Wi-Fi AP close by on Wi-Fi ch 4, ESP-NOW devices, Bluetooth, an Xbox Wireless Adapter).
  - Red: RSSI −67 to −79 dBm, mostly 90–99 reports a second, with dips to 36–55 during CRC-error bursts
    (`bad` up to 75 a second): interference.
  - Blue: −83 to −85 dBm, held a link for only about 12 of 60 s (at −74 to −77 while it did). At the edge
    of receive range.
  - Red's player number flips 1↔2 each time Blue links and unlinks.
  - For comparison, open air near the controllers on the cable gave about −40 dBm.
- **Fixed: hang when every channel is cooling (2026-11-10).** On a weak link every channel looks bad, so
  automatic moves cooled all of 2–80 within a few minutes. `scan_step` then looped forever, USB froze, and
  only a replug recovered it (a PC restart doesn't cut USB power). The loop is now bounded and clears the
  cooldowns. Flashed on the HTPC; not yet seen under the condition that triggered it.
- **Fixed: hunting parked on noisy channels (2026-11-10).** CRC-bad packets refreshed the "heard something"
  timer, so a channel full of Wi-Fi/ESP-NOW noise (300–570 bad a second on channel 6) stopped the hunt for
  good and the controllers never connected. Only CRC-good packets count now. Checked on the HTPC: after a
  noise burst on 33 the bridge hunted on and found Blue on 68 (−77 dBm, 85–94 reports a second).
- **Verdict on range:** even side by side at the couch the controllers read −73 to −81 dBm. The board is a
  Pro Micro nRF52840 clone with a "C3" chip antenna; the receive side is the limit. Next: the antenna mod,
  else a board with a proper external antenna (e.g. a HolyIoT nRF52840 module).

## Known issues and limits

- macOS shows only the first of the two Xbox interfaces, and none at all in the default (Windows composite)
  USB mode. Build with `USB_XBOX_IDS=1` to get a pad on the Mac.
- **COM port on Windows: fix untested.** With 045E:028E the Xbox composite driver claims the whole device and
  the CDC port never enumerates. The default build now uses 1209:0001 + MS OS 2.0 descriptors to fix that;
  waiting on the HTPC test.
- Rumble and the polling rate are not supported (the downstream path is unknown). The extra buttons and back
  paddles arrive in the report but aren't mapped yet.
- CRC-bad counter around 40 a second with both controllers active (collisions; retransmits hide them).
- No hunting for a second controller while the first one is connected.
- Weak links drop instead of riding out losses: a few lost reports in a row and the controller times
  out and searches again. A more tolerant timeout would help weaker setups (not needed with the cable).
- One plug-in on Windows powered the board (controllers paired) but never enumerated; a replug fixed it.
  Probably a badly seated plug; not reproduced on the retry.
- Not tested: a 30-minute-plus session, wake after hours of sleep (the user chose to skip these).

## Next step: HTPC test (in progress, 2026-11-10)

The board already runs the latest firmware (flashed from the Mac); just move it to the HTPC on the extension cable.

1. Device Manager: two Xbox 360 controllers **and** a COM port, no warning icons.
   If the pads don't get a driver, check which IDs `C:\Windows\INF\xusb22.inf` lists
   (`findstr /i "USB\\" C:\Windows\INF\xusb22.inf`) and try `XUSB20`; otherwise fall back to WebUSB.
   Fallback build that worked on Windows: `USB_XBOX_IDS=1` (firmware.md, "USB").
2. Gamepad tester: both pads; Blue is player 1; one controller alone is player 1; turning a controller off
   releases its buttons.
3. Open the hosted page in Edge, connect, and read **Signal** with the controllers at the couch. This is the
   baseline for the antenna mod below.

## To do (after the HTPC test)

- **Antenna mod (user):** the board's chip antenna is a "C3" part, likely the same CrossAir CA-C03 as in
  [this ESP32-C3 SuperMini mod](https://peterneufeld.wordpress.com/2025/03/04/esp32-c3-supermini-antenna-modification/):
  31 mm of 1 mm wire soldered across the antenna, about 6–10 dB better receive there (receive measured only;
  some readers report worse sending). Measure Signal and Retries on the page before and after. Unverified on
  this board. Alternative: an nRF52840 board with an external antenna (plan §7).
- **Investigate unlocking 1000 Hz polling.** Today the controllers send about 100 reports a second.
  `A5 50 0A 01` was acked by the controller but changed nothing ([vendor-commands.md](vendor-commands.md),
  "Polling rate"). Ideas: set 1000 Hz in Space Station with the official dongle and capture the radio rate and
  traffic; check whether it needs the flash-save command or gyro off; find the downstream path (below), which
  the bridge needs to send settings at all. Check the bridge copes: ack turnaround, two controllers on one
  channel at 10× the airtime, USB IN at 1 ms.
- **Map the back paddles and extra buttons:** label bytes 5, 6, 22 (protocol.md) with the user pressing each
  one, then map them to Xbox buttons. About 2 minutes of the user's time.
- **Rumble:** games' rumble already reaches the bridge (`xinput_rumble_*`); the radio path to the controller is
  unknown. Capture it with the official dongle (same session as pairing).
- **Optional remote access:** SSH on the HTPC would let Claude read the bridge's COM port from the Mac.

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

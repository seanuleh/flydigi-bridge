# Firmware

Board: nRF52840 with the Adafruit nRF52 bootloader. Built as `adafruit:nrf52:feather52840`. The dev board is
a Pro Micro nRF52840 (nice!nano clone): no user button, one LED. Build and flash commands are in the README.

## firmware/fd_bridge — the bridge

Files:
- `fd_bridge.ino`: radio, player mapping, report translation, telemetry, serial console
- `store.cpp` / `store.h`: slot table and player order in flash
- `xinput.cpp` / `xinput.h`: USB Xbox 360 interfaces, MS OS 2.0 descriptors

### Radio

- **Slots:** two controller slots on radio pipes 0 and 1.
  - Addresses, names and the player order are kept in flash (`/fdbridge.cfg` on the core's LittleFS,
    with magic, version and CRC-32). The first boot, or a corrupt file, writes the defaults: slot 0 = Red
    `69cc966cee`, slot 1 = Blue `66cc69c6ee`, Blue first in the player order.
  - TX power +8 dBm (the maximum; affects only our acks).
  - Pipes 0 and 1 have separate bases and prefix bytes, so any two addresses fit.
  - Flash is only written by config commands. The radio is stopped around the write; connected controllers
    ride it out (checked: no drop).
- **Receiving:** ESB receive on one channel (default 12).
  - Each packet's END event goes through PPI to start TIMER3, and +20 µs later TXEN fires.
  - The ack ends 192 µs after the packet ends, which matches the real dongle.
  - The interrupt handler picks the reply payload and sets `TXADDRESS = RXMATCH`, so the reply goes out on
    that controller's address.
- **Replies:**
  - report → `02 aa <r30+1> 00`
  - anything else → `02 aa <b19+1> 00` (`other_mode=1`; see protocol.md, "Reconnect handshake")
- **Channel quality (automatic moves):** controllers follow the bridge to another channel, but only to
  channels in their own search set.
  - Every second the bridge takes the average report rate of the connected controllers and keeps a score
    per channel. Below 85 a second for 3 seconds: the channel cools down for 5 minutes and the bridge moves
    to the best-scoring channel where it has had controllers before; with none, it hunts (skipping cooling
    channels). Hunting pauses 6 s after a move so the controllers can follow.
  - A channel that stays good for a minute is saved in flash and used at the next boot.
  - `channel <n>` fixes the channel and turns this off; `channel auto` turns it back on.
  - Moves are reported as `{"type":"log","msg":"channel 55: 35 reports/s, moving to 40"}`.
  - Tested 2026-11-10 on the Mac: channel 55 gave about 35 reports a second on both controllers (Wi-Fi band),
    channel 12 gave 100. Forced back to 55, the bridge moved to 40 after 3 s and both controllers followed
    with no drop. A jump to a channel outside their set (25) cost a 6 s drop, hence the "seen before" rule.
- **Channel hunting:**
  - If no slot has had a report for 1 s and nothing was heard for 300 ms, step through channels 2–80,
    25 ms each.
  - It stops on the first CRC-valid packet.
  - It doesn't yet hunt for a missing controller while another one is connected.

### USB

- Product "Flydigi Bridge". Two USB modes, picked at build time with `USB_XBOX_IDS`:
  - **`0` (default), Windows composite mode:** VID/PID `1209:0001` (pid.codes test PID), bcdUSB 2.1, and
    Microsoft OS 2.0 descriptors. The set holds a CCGP feature (load usbccgp) and a function subset per Xbox
    interface with compatible ID `XUSB10`, so the in-box xusb22 driver should bind each pad while the CDC port
    enumerates too. **Untested on Windows.** macOS won't see a pad in this mode.
  - **`1`:** VID/PID `045e:028e`. macOS sees a pad; Windows binds the whole device to the Xbox composite
    driver and the COM port never appears. Build with
    `--build-property "compiler.cpp.extra_flags=-DUSB_XBOX_IDS=1"`.
  - The Adafruit core always links its own BOS descriptor, which announces a 0xB2-byte MS OS 2.0 set on vendor
    code 2. `xinput.cpp` overrides the weak `tud_vendor_control_xfer_cb` and serves its own set of exactly
    that length (padded with a dummy `FdbPad` registry property).
  - Windows caches MS OS descriptors per VID/PID/bcdDevice. Bump `USB_BCD_DEVICE` whenever they change.
- Flash from Windows: double-tap reset, then copy a UF2 (`uf2conv.py -f 0xADA52840 -c`) to the drive.
- One Xbox 360 interface per slot (class `FF/5D/01`, IN 32 B at 1 ms, OUT 32 B), implemented as a TinyUSB app
  class driver (`usbd_app_driver_get_cb`).
- Plus the core's CDC serial port, used as the debug console.
- Reports are 20-byte Xbox 360 input reports. Rumble OUT packets (`00 08 00 L R`) are parsed into
  `xinput_rumble_*`, but **not forwarded** to the controller.
- **macOS:**
  - Apple's driver only exposes the **first** interface, so one pad is visible.
  - The first plug-in asks "Allow accessory".
- **Windows:** see status.md.

### Players

- Each slot is "linked" once reports arrive, and unlinked after 3 s without reports, so a short dropout
  doesn't reshuffle players.
- Linked slots take the Xbox interfaces in player order: with both linked, the first in the order
  (default Blue) is player 1. A controller on its own is always player 1 (the user's choice, 2026-11-10).
  When the second controller arrives, the first may move from player 1 to player 2.
- An interface whose controller is gone, or silent for 150 ms, gets one neutral report so no button
  stays held.
- On Windows both Xbox interfaces always exist, so an unused one still looks like a connected pad.

### Telemetry

Per slot, per second: `rate` = reports with a new counter (byte 30; about 100 when healthy), `dup` = repeats
of the last counter (the controller missed our ack and resent), `rssi` = smoothed signal in dBm. The web page
shows loss as `100 - rate` (approximate: assumes a steady 100 reports a second).

### Console (word commands, JSON replies)

The web page uses these. Each reply is one JSON object per line. Lines that aren't a known word fall through
to the debug commands below.

| Command | Effect |
|---|---|
| `status` | `{"type":"status","fw","ch","hunt","bad","order":[p1,p2],"slots":[{slot,name,addr,kind,link,player,rate,dup,rssi,usb}]}`; `link` is `up`, `lost` (silent but still holding its player), `down` or `empty` |
| `watch <0\|1\|2>` | 0 = off (the old `# stat` line every 2 s comes back); 1 = status every 200 ms; 2 = status plus `{"type":"input",slot,btn,lt,rt,lx,ly,rx,ry}` every 20 ms per slot (XInput values) |
| `name <slot> <text>` | Rename (15 chars max), saved |
| `forget <slot>` | Empty the slot, saved |
| `addr <slot> <10 hex> [name]` | Put an on-air address in a slot by hand, saved |
| `order <p1> <p2>` | Player order (slot numbers), saved |
| `channel <auto\|n>` | Hunt, or fix the channel |
| `learn <slot> [s]` | Learn mode (below), default 60 s. Progress: `{"type":"learn","state":"listening",slot,ch,left,candidates:[{addr,hits,ch,rssi}]}` every 500 ms, then `confirming`, then `done` (with `addr`) or `failed`/`cancelled` (with `reason`) |
| `cancel` | Leave learn mode, slot unchanged |
| `defaults` | Back to Red + Blue, saved |
| `version`, `reboot`, `bootloader` | `bootloader` enters the UF2 drive |

Other replies: `{"type":"ok","cmd":…}`, `{"type":"error","msg":…}`.

### Learn mode

Adopts a searching controller's existing address (plan: learn-and-pairing-plan.md §4).
- Serving stops (other controllers drop and come back afterwards). The radio goes promiscuous: BALEN 1,
  prefixes `AA`/`55`, base 0, 34 raw bytes, no hardware CRC. It hops channels 2–80, 15 ms each, and stays
  on a channel while valid packets arrive.
- Each capture is searched at bit offsets 0–24 for an ESB packet (5-byte address, 9-bit header, payload,
  CRC-16 init `FFFF` poly `1021` over all of it). Addresses stored in *other* slots are ignored.
- 5 valid packets from one address → it goes on the slot's pipe, normal serving resumes on that channel,
  and the slot is saved once 20 reports arrive (6 s limit; otherwise the old slot comes back).
- Tested 2026-11-10: blue forgotten, then learned back in about 1 s (found on channel 5), while red stayed
  in its slot.

### Debug console (115200, any baud)

One letter, optionally followed by a number (`cancel` is rejected, not read as `c`).

| Command | Effect |
|---|---|
| `c<ch>` | Set the channel |
| `S` | Toggle channel hunting |
| `s` | Print stats now |
| `v<slot>` | Toggle a raw dump of reports (`R<n> …`) and other packets (`O<n> …`) |
| `p<mask>` | Enabled pipes/slots (bitmask) |
| `t<us>` | Ack delay after packet END (default 20) |
| `h<mode>` | Reply to non-report packets: `0` `02`, `1` `02 aa b19+1 00` (default), `2` none, `3` `02 aa b19 00`, `4` clock, `5` mixed |

Stats line, every 2 s while `watch` is 0:

```
# stat ch=12 bad=8 turn=205us | c1 UP P2 rep=677 dup=1 oth=0 usb=1 rssi=-40 | c2 UP P1 ...
```

- `bad` = CRC failures (mostly collisions)
- `turn` = measured ack turnaround
- `rep` = reports received
- `dup` = resent reports
- `oth` = other packets
- `P<n>` = player number (`P0` = none)
- `usb` = reports sent to the host

## firmware/fd_sniff — sniffer

Prints every received packet as `P <us> <ch> <rssi> <pipe> <crc> <len> <pid> <noack> <hex>`.

| Command | Effect |
|---|---|
| `a` | All channels 0–100 |
| `l4,12,28` | Channel list |
| `d<ms>` | Dwell per channel (`0` = never hop) |
| `h<ms>` | Extra dwell after a hit |
| `b` | Toggle printing CRC-bad packets |
| `x<10 hex>` | Address, in on-air order |
| `m<balen>,<pre0>,<pre1>,<base0>,<base1>,<raw>` | Raw register mode |
| `n` | Back to normal mode |

- `m1,aa,55,00000000,00000000,1` is **promiscuous** mode: it prints `R <us> <ch> <rssi> <pipe> <34 raw bytes>`.
  Feed that to `tools/find_addr.py`.
- Timestamps come from the cycle counter, in µs. They wrap after about 67 s.

## firmware/fd_dongle — single-controller emulator

The first working prototype. It uses the plain `DISABLED_TXEN` turnaround (about 160 µs), and replies `02` to
the connect message, which works for a controller that reconnects straight into reports. Superseded by fd_bridge.

## firmware/legacy

June 2026 sketches: a promiscuous ESB scanner and a BLE scanner. The ButteRFly UF2 lives here locally (not in git).

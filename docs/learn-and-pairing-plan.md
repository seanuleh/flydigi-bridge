# Plan: Learn mode, full pairing, and the USB web interface

Status: **§8 steps 2–4 done on 2026-11-10** (slot table, console protocol, telemetry, web page v1, learn mode;
see firmware.md; page hosted at https://seanuleh.github.io/flydigi-bridge/). Pairing (§5) not started. Written 2026-11-10. Prerequisite: the dual-controller Windows test
(see "Order of work" at the bottom, and `docs/status.md`); its result decides which USB interface the web page uses.

## 1. Where we are

- `fd_bridge` serves two Vader 4 Pro controllers at once, using radio pipe 0 and pipe 1.
- Their addresses are **hardcoded** in `fd_bridge.ino`:
  - red = `69 cc 96 6c ee`
  - blue = `66 cc 69 c6 ee`
- Both addresses came from the official dongles' earlier pairing. The bridge reuses them; it never pairs.
- A controller that can't find its dongle keeps calling out on its stored address:
  - it stays on one channel;
  - it alternates 4-byte pings (about 2,000 a second) with a 21-byte packet every 10 ms.

  Learn mode is built on this behavior.
- The pairing protocol (how a controller gets a new address) is **unknown**. It has never been captured.

### Radio constraint that shapes both features

The nRF52840 radio can listen for 8 addresses at once (pipes 0–7), with these rules:

| Pipe | Base (address bytes 0–3, on-air order) | Prefix (address byte 4) |
|---|---|---|
| 0 | its own (`BASE0`) | its own |
| 1–7 | **shared** (`BASE1`) | each its own |

Consequences:
- **Two arbitrary addresses always fit**, on pipe 0 and pipe 1. That's why red and blue work.
- **A third arbitrary address only fits if bytes 0–3 match an existing pipe 1–7 address.** This is
  essentially never true for addresses we didn't choose.
- With **full pairing** we choose the addresses: one shared base and seven distinct prefixes, so up to
  7 controllers fit, plus pipe 0 for one learned "legacy" controller.

## 2. Control interface: web page over USB (chosen)

The board has no button: it's a Pro Micro–style nRF52840 (nice!nano clone) with one LED. Wi-Fi isn't an
option, so the web page talks to the bridge over its USB connection, from Chrome or Edge, with nothing
to install.

### Option A — Web Serial over the existing USB serial port (preferred)

- The bridge already exposes a USB serial port next to the Xbox interfaces; it's the debug console
  used throughout development.
- Chrome and Edge can open it from a web page with `navigator.serial`, after a one-time port picker.
- Windows 10 and 11 include a serial driver, so nothing needs installing.
- **Risk:** the Windows test may show that the Xbox driver dislikes sharing the device with a serial
  port. If dropping the serial port is the only fix, use option B.

### Option B — WebUSB vendor interface (fallback)

- Replace the serial port with a vendor-specific interface, plus Microsoft OS 2.0 descriptors so
  Windows loads its generic USB driver automatically.
- The page uses `navigator.usb`. TinyUSB has WebUSB support (`Adafruit_USBD_WebUSB`), and it can also
  advertise a landing-page URL.
- More work than option A, and the serial debug console disappears (debug could move into the same
  vendor channel).

### Protocol between the page and the bridge

Line-based, so the same commands still work from a plain serial terminal for debugging.

**Host → bridge** (one command per line):
```
status                      one-shot status
watch <0|1|2>               0 = off, 1 = status every 200 ms, 2 = status + live input every 20 ms
learn <slot> [timeout_s]    start learn mode for a slot (default 60 s)
pair <slot> [timeout_s]     start pairing mode for a slot (once implemented)
cancel                      leave learn/pair mode
forget <slot>               delete a slot
name <slot> <text>          friendly name ("Red", "Blue")
swap <a> <b>                swap slots, i.e. which Xbox player number a controller gets
channel <auto|n>            channel policy
reboot | bootloader         maintenance
```

**Bridge → host:** one JSON object per line, with a `type` field:
```json
{"type":"status","ch":27,"mode":"run","slots":[{"slot":0,"name":"Red","addr":"69cc966cee","link":"up","rate":100,"rssi":-48,"lost":0.2}, ...]}
{"type":"learn","state":"listening","ch":5,"candidates":[{"addr":"66cc69c6ee","rssi":-44,"hits":12}]}
{"type":"learn","state":"done","slot":1,"addr":"66cc69c6ee"}
{"type":"input","slot":0,"buttons":"…","lx":128,"ly":128,"rx":128,"ry":128,"lt":0,"rt":0}
{"type":"log","msg":"…"}
```

### Web page

- A single self-contained HTML file.
- Hosted as a published page (I can publish it as an artifact); Web Serial needs https, which that
  provides. Also kept in the repo as `web/index.html`, which works from `localhost` too.
- Sections:
  - **Connect**: the "Select bridge" button, which opens Chrome's port picker.
  - **Slots**: one card per slot showing:
    - name and color;
    - link state, report rate, signal strength and packet loss;
    - a live input preview (sticks, triggers, buttons);
    - Learn / Pair / Forget / Rename buttons.
  - **Radio**: the current channel, a channel-hunting indicator, and collision and error counters.
  - **Firmware**: version, plus a reboot-to-bootloader button.
- The wizard text in learn mode should say: "Unplug the controller's original dongle, then turn the
  controller on."

## 3. Storage (shared by both features)

- Keep the slot table in flash with the core's `InternalFileSystem` (LittleFS), as a file such as
  `/slots.bin` with a version field and a CRC.
- Each slot holds:
  - `addr[5]`
  - `name[16]`
  - `kind`: learned (legacy) or paired (ours)
  - `pipe`
  - `flags`
- Also store the bridge's own pairing base address, created once (see §5).
- **Flash write stalls:** erasing flash stalls the CPU for about 85 ms per page. That's enough to
  briefly drop connected controllers.
  - Only write in learn, pair, forget and rename, never during normal running.
  - Before writing, stop the radio cleanly so controllers just retransmit and resync.
- Pipe allocation:
  - Pipe 0: the first learned controller (its own base).
  - Pipe 1: the second learned controller (its own base); in that case paired controllers can't use
    pipes 2–7.
  - Pipes 1–7: paired controllers once the bridge's own base is in use.

  §6 covers the rule that resolves this.

## 4. Learn mode ("pairing light")

**Goal:** adopt a controller's existing address without negotiating anything. The controller keeps the
address its original Flydigi dongle gave it.

### Flow

1. The user clicks **Learn** on a slot in the web page.
2. The bridge enters `LEARN`:
   - It stops serving the other slots. Their controllers start searching and come back afterwards,
     usually within seconds. The page warns about this.
   - Leaving out serving during learn keeps it simple. A later version can time-slice instead:
     serve for 90 ms, sniff for 10 ms.
3. **Promiscuous scan.**
   - Configure the radio the way `fd_sniff` mode `m1,aa,55,…,1` does: a 2-byte match on the preamble,
     34 raw bytes, no hardware CRC.
   - Hop channels 2–80 with about 15 ms dwell. A searching controller sends roughly 30 pings in that
     time, so one sweep takes about 1.2 s.
4. **Find and verify addresses in software.** This is better than the frequency counting used in
   `find_addr.py`.
   - For each raw capture and each bit offset 0–16, treat the next 5 bytes as an address and the
     following 9 bits as the packet header. Read the length from the header, then compute CRC-16
     (init `FFFF`, polynomial `1021`) over address, header and payload.
   - A match means a real ESB packet. The bit-level code already exists in `decode_esb.py` and
     `find_addr.py`; port it to C.
   - Accept an address once it has **at least 5 CRC-valid hits**.
   - Ignore addresses that are already stored in a slot.
   - Record each candidate's signal strength and channel.
5. **Choose:**
   - One candidate: use it.
   - Several (for example a friend's controller also searching): send the list to the page and let
     the user pick, defaulting to the strongest signal.
6. **Confirm:**
   - Switch to normal mode with the candidate on the slot's pipe, on the channel where it was heard.
   - Wait for the handshake and the first gamepad reports (up to 5 s).
   - Success means steady reports. Only then write the slot to flash.
7. Report `learn done` to the page. The page shows live input so the user can check it's the right
   controller, for example "press A to confirm".

### Edge cases

- **Official dongle still plugged in:** the controller connects to it and stops searching, so learn
  mode hears nothing and times out. The page should mention this when it times out.
- **Controller in Bluetooth mode:** nothing on the radio at all (this happened once during testing).
  Same timeout hint: "check the controller is in 2.4 GHz mode".
- **Slot conflict:** two learned addresses use up pipes 0 and 1, which leaves no room for paired
  controllers (see the pipe table). Show this in the page.
- **Duplicate:** an address already in another slot means "already learned in slot N".

### Effort

Small to medium:
- the C port of the CRC search;
- the learn state machine;
- LittleFS storage;
- serial command handling.

No unknowns: every step was done by hand on 2026-11-10.

## 5. Full pairing mode

**Goal:** the bridge pairs a controller from scratch, the way the official dongle does, assigning an
address **we** choose. No official dongle is ever needed, and up to 7 controllers fit.

### Phase A — Research (needs you and an official dongle, about 1 session)

1. **Find out how pairing is started.**
   - The controller button combo: check the manual or Flydigi's site.
   - Whether the dongle needs a button press, or a USB command from Space Station.
   - The repo docs don't cover this. The Space Station command tables list "Dongle info" (`A5 11`) and
     upgrade-mode commands but no obvious pairing command, so check the decompiled tables for a
     `pair`/`bind` entry.
2. **Capture a real pairing.**
   - `fd_sniff` in promiscuous mode (any address) across all channels.
   - Also try 1 Mbps and the BLE 1M/2M modes, in case pairing uses a different radio mode.
   - Then a fixed-channel capture once the pairing channel is known.
   - Pair the same controller **2–3 times** and diff the captures to see what stays fixed and what
     changes (random or derived address).
3. **Things to look for:**
   - A fixed, well-known pairing address and channel.
   - Which side proposes the new address.
   - Whether the address is derived from an ID:
     - the dongle's ID (`A5 11` dongle info);
     - the controller's "MAC" from `A5 10`, which was `69 5c 61 36` for red. Red's radio address is
       `69 cc 96 6c ee`: is there a relation?
   - Whether there's a key exchange or any encryption. Normal traffic is plaintext, so probably not.
   - What the controller stores: just the address, or also a dongle ID it checks later.
4. **Side effect:** re-pairing the controller to its official dongle may give it a new address. That's
   harmless; read the new one with learn mode afterwards.

### Phase B — Decode

- Write the pairing exchange into `docs/protocol.md`, as was done for the reconnect handshake.
- Replay it from `fd_sniff`/`fd_dongle`-style test firmware to confirm it works, before building it
  into the bridge.

### Phase C — Implement in the bridge

- **The bridge's own address space:**
  - Create one shared base (4 bytes) the first time the bridge starts, from the chip's unique ID
    (`FICR->DEVICEID`), so two bridges never collide.
  - Store it in flash.
  - Paired controllers get `base + prefix[n]`, with prefixes chosen to avoid the radio's bad
    address patterns (long runs of 0/1 bits, or bytes like `0x55`/`0xAA` that look like the preamble).
- **The `PAIR <slot>` state:**
  - Go to the pairing channel/address.
  - Act as the dongle in the captured exchange, offering the slot's new address.
  - Confirm with a normal handshake on the new address.
  - Store the slot.
- The page shows "Hold <combo> on the controller" plus progress.

### Risks

- Pairing may use something we can't easily copy, such as a Telink-specific radio mode or a check of
  the dongle's identity. In that case learn mode stays the supported path, and the research still
  tells us whether the address is derived from an ID.
- Pairing a controller to the bridge breaks its pairing with its official dongle. Make this clear in
  the page.

### Effort

- **Phase A:** 1 session with you (button presses, plugging dongles).
- **Phases B and C:** medium, depending on what the capture shows.

## 6. Slot and pipe model (decide during implementation)

| Situation | Pipe 0 | Pipes 1–7 |
|---|---|---|
| Only learned controllers (current state) | learned #1 | pipe 1 = learned #2 |
| Mixed | learned #1 | paired controllers (bridge base), up to 7 |
| Only paired | free (could hold a legacy learned one) | paired, up to 7 |

Rule: **at most two learned controllers**, and the second one blocks paired slots. The page shows this
plainly: "Pair (recommended) vs Learn (legacy, max 2)".

Separately, each slot maps to a fixed Xbox interface, which becomes the player number. Keep that
mapping stable across reboots: slot 0 is always player 1, and so on. `swap` changes it.

## 7. Hardware options (keep open)

| Board | Button | LED | Notes |
|---|---|---|---|
| **Current Pro Micro nRF52840 (nice!nano clone)** | no | 1 (single color) | Works fine with the web page. Bootloader accepts Feather builds. |
| Nordic **nRF52840 Dongle (PCA10059)** | yes | RGB | USB-A stick that suits an HTPC. Ships with a Nordic DFU bootloader; an Adafruit UF2 bootloader can be flashed. Small PCB antenna. |
| Seeed **XIAO nRF52840** | reset only | RGB | Tiny, USB-C, good bootloader support. |
| nRF52840 module with **u.FL/external antenna** (e.g. Ebyte E73 / Holyiot boards) | varies | varies | Best range and reliability if the HTPC sits in a cabinet. |
| Adafruit Feather nRF52840 Express | yes | NeoPixel | Same pinout as the build target; bulky. |

Keep the firmware independent of the board:
- put a `board.h` with pins for the optional button and LEDs;
- the web page is the main interface, while the button and LED are an optional extra.

If a board with a button turns up later, add these (no other changes needed):
- long-press = learn the next free slot;
- very long press = forget all;
- LED patterns for run, learn and pair.

## 8. Order of work

1. **Now:** test two controllers on Windows (the HTPC).
   - Do two Xbox controllers appear?
   - Does the serial port stay usable next to the Xbox interfaces? This decides option A or B in §2.
   - Basic reliability: sleep and wake, power-on order, a 30-minute session.
2. **Shared infrastructure:**
   - the LittleFS slot table;
   - remove the hardcoded addresses, seeding the table from them once for migration;
   - the serial command/JSON protocol;
   - status telemetry (rate, signal strength, loss).
3. **Web page v1:** connect, slots, live input, forget, rename.
4. **Learn mode** (§4), then add it to the page.
5. **Pairing research** (§5 phase A), with you.
6. **Pairing implementation** (§5 phases B and C), then add it to the page.

### Acceptance tests

- **Learn:**
  - Start from empty flash and learn both controllers through the page.
  - After a reboot both reconnect without any action.
  - Forget one, and it's ignored after that.
- **Pair:**
  - Pair a controller with no official dongle involved.
  - Survives a reboot.
  - Three or more controllers at once, if available.
- **Reliability** (the main goal):
  - Any power-on order.
  - Wake after hours of sleep.
  - Both controllers at once for a 2-hour session with no drops visible in-game.
  - The page's loss counter stays under 1%.

## 9. Open questions for you

1. What's the button combo for pairing a Vader 4 Pro with its dongle? (Needed for §5 phase A.)
2. ~~How many controllers?~~ Answered 2026-11-10: the user wants more than two on one bridge, so full
   pairing is on the to-do list (status.md, "Then"). Original question: how many controllers do you ever expect at once: 2, or up to 4 for guests? This decides how much
   pairing matters compared to learn mode.
3. Any preference for player-number assignment, such as red always player 1?

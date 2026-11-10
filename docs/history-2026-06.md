# History: June 2026 session (superseded)

Kept for context only. **Several conclusions here are wrong**, most importantly "the payload is
AES-encrypted": it isn't. The 9-bit ESB packet header made the raw bytes look random. For current facts
see [protocol.md](protocol.md). The 2026-06 address `66:cc:69:c6:ee` turned out to be the controller "blue".

Still useful: ButteRFly-on-nice!nano build notes, and the Space Station installer analysis (no firmware is
bundled; it downloads on demand).

## Controller in wired USB mode
- VID: 0x045E (Microsoft), PID: 0x028E (Xbox 360 Controller)
- Strings: "Flydigi" / "Flydigi VADER4"
- Protocol: XInput (vendor-specific class 0xFF)
- Speed: Full Speed USB (12 Mbps)
- macOS driver: XboxUSBDevice

## Wireless hardware
- Radio chip: Nordic nRF52 series (confirmed via Nordic press release for earlier Apex model)
- FCC filing: 2AORE-F4, filed 2024-06-27
- PCB label: FLYDIGI_F4_HALL_V08
- Frequency range: 2402–2480 MHz
- Antenna: PCB trace near USB-C port

## Wireless protocol
- NOT standard Bluetooth LE (BLE scanner showed no advertising from controller)
- NOT BLE 2M PHY (despite FCC saying "Bluetooth" — dongle mode is proprietary)
- Almost certainly **Nordic Gazell or Enhanced ShockBurst (ESB)**
- Frequency hopping detected: traffic distributed across channels
- Channels with activity: 29 (RSSI -47), 39 (RSSI -56), 55 (RSSI -58 to -64)
- Data rate: 2Mbps (consistent with modern nRF52 ESB/Gazell)

## What we couldn't determine yet
- Exact ESB/Gazell address (needed for proper packet capture)
- Full channel hopping table
- Payload format / report structure

## Tools used
- nice!nano (nRF52840) running custom ESB promiscuous scanner (Arduino/TinyUSB)
- Promiscuous sniffing via preamble-address trick (0xAAAAAAAA / 0x55555555)
- ButteRFly firmware (whad-team/butterfly) — NOW RUNNING on nice!nano
- WHAD Python tools (wsniff, whadup) — installed and connected

---

## ButteRFly on nice!nano — STATUS: WORKING ✓

### Build setup (all at /tmp/butterfly/)
- Toolchain: ARM GNU Toolchain 15.2.rel1 at `/tmp/arm-gnu-toolchain-15.2.rel1-darwin-arm64-arm-none-eabi/`
- SDK: nRF5 SDK 17.1.0 at `/tmp/.sdks/nRF5_SDK_17.1.0_ddde560/`
- Board target: `BOARD_NICENANO` (added to Makefile)
- Build command: `cd /tmp/butterfly && make PLATFORM=BOARD_NICENANO SDK_ROOT=/tmp/.sdks/nRF5_SDK_17.1.0_ddde560/`
- Flash: `make PLATFORM=BOARD_NICENANO SDK_ROOT=... dist` → copies UF2 to /Volumes/NICENANO

### Key patches applied to build successfully
1. **Makefile**: Added `BOARD_NICENANO` platform with `-Wall -Wno-array-bounds -Wno-stringop-overread`, `-std=gnu++17 -fpermissive`, no `-Werror`, uses `app_timer.c`
2. **config/nicenano/nicenano.ld**: App start at `0x26000` (Adafruit bootloader, not 0x1000 like MDK_DONGLE)
3. **config/nicenano/custom_board.h**: LED_1 on P0.06 (active HIGH, LEDS_NUMBER=1), BUTTONS_NUMBER=0
4. **SDK patch** `app_usbd_cdc_acm_internal.h`: Changed `enum app_usbd_cdc_acm_user_event_e;` → `enum app_usbd_cdc_acm_user_event_e : int;`
5. **SDK patch** `app_usbd_cdc_acm.h`: Added `: int` to enum typedef
6. **SDK patch** `app_usbd_cdc_acm_internal.h`: Reordered designated initializers in `APP_USBD_CDC_ACM_INST_CONFIG` macro to match struct field order
7. **serial.h**: Set `USBD_POWER_DETECTION false` (bypasses POWER peripheral events, calls usbd_enable/start directly)
8. **SDK Makefile.posix**: Toolchain path set to `/tmp/arm-gnu-toolchain-15.2.rel1-darwin-arm64-arm-none-eabi/bin/`

### USB enumeration
- VID: 0xC0FF, PID: 0xEEEE
- USB Vendor Name: "WHAD", USB Product Name: "ButteRFly dongle"
- Appears as `/dev/cu.usbmodemXXXXXXXX` (serial number may vary)
- macOS shows "Allow accessory to connect? WHAD ButteRFly dongle" prompt — click Allow
- WHAD Python tools installed: `pip3 install whad --break-system-packages`

---

## ESB ADDRESS FOUND ✓ (2026-06-03)

**Address: `66:cc:69:c6:ee`**
**Channel: 41**  (no hopping observed during 20s lock — single channel)
**Data rate: 2 Mbps ESB** (WHAD default; CRC validates)
**RSSI: -55 to -57 dBm** at desk distance

### How it was found
Scripted `whad.esb.Scanner` discovery (`esb_address_scan.py`). Note: WHAD 2.x ships
with a bug — `Scanner.__init__` calls `super().sniff(...)` but the base class
method is `start_sniff(...)`. The script monkey-patches around it.

### Packet structure (14 bytes raw, CRC-valid)
Example: `aa 66cc69c6ee 1081 55 2280 6f14 00`
| offset | bytes | meaning |
|--------|-------|---------|
| 0      | `aa`               | preamble |
| 1–5    | `66 cc 69 c6 ee`   | 5-byte ESB address |
| 6–7    | `10 81`            | PCF (9-bit): length, PID, NO_ACK (constant so far) |
| 8      | `55`               | constant — likely report type / sync |
| 9–12   | 4 bytes            | sequence/data — see notes |
| 13     | `00`/`80`          | likely CRC tail / framing |

The 4 variable bytes change continuously even when idle — almost certainly include
a sequence counter or timestamp + battery/state. Need button-press correlation to
map controls. Run `python3 esb_capture_log.py 30 idle.log` while idle, then again
while pressing specific buttons, and diff.

### Verification scripts
- `esb_address_scan.py` — auto-discover addresses (patched Scanner)
- `esb_sniff_addr.py 66:cc:69:c6:ee 41 20` — lock onto address, print packets
- `esb_capture_log.py 30 capture.log` — timestamped log capture for diffing
- `esb_guided_capture.py` — interactive, prompts through button/stick sequence, tags log
- `esb_full_scan.py 60` — long scan to find any additional addresses / channel-hopping
- `diff_captures.py` — per-byte distribution diff between log files

---

## 🛑 BLOCKER: Payload appears whitened/encrypted (2026-06-03)

Captured ~7500 packets across 14 distinct input states (idle, hold A/B/X/Y, stick
extremes in all directions, triggers, dpad). For every state, bytes 9-13 show
**identical statistical distribution**:

| byte | unique values | distribution    | interpretation                  |
|------|---------------|-----------------|---------------------------------|
| 8    | 1 (0x55)      | constant        | report type or sync byte        |
| 9    | 128           | uniform 0-127   | counter (delta 2-3) OR encrypted|
| 10   | 2 (0/0x80)    | ~50/50 single bit | PID bit or encrypted          |
| 11   | 128           | uniform 0-127   | encrypted payload OR CRC        |
| 12   | 256           | uniform 0-255   | encrypted payload OR CRC        |
| 13   | 2 (0/0x80)    | ~50/50 single bit | encrypted bit OR end marker   |

The modal value of each byte varies randomly between input states — exactly what
you'd see if the bytes were the output of a stream cipher / whitening LFSR keyed
by sequence counter. Wiggling sticks does not produce any visible bit pattern
correlated with stick direction.

Frequency hopping confirmed: same address `66:cc:69:c6:ee` observed on channels
12 and 41 within a 60s scan.

### Why this is plausibly encrypted, not just whitened
Nordic ESB has optional **payload whitening** using a 7-bit LFSR seeded with the
radio channel. This is normally undone by the receiver HW automatically. WHAD's
ButteRFly may or may not de-whiten — worth testing.

More likely though: Flydigi's firmware uses **AES-128-CCM** on payloads (common
for gamepad SDK derivatives). The 5-byte address + 7-bit counter + 1-bit PID
plausibly forms the nonce. Without the AES key, payload bytes look uniform.

### Three paths forward

**Path 1 — De-whitening test (cheap, hours):**
Apply Nordic's 7-bit whitening LFSR (poly x⁷+x⁴+1, seed = channel | 0x40) to the
4 payload bytes and re-check distribution per input state. If de-whitened bytes
cluster on state, the protocol is unencrypted but ButteRFly didn't undo whitening.

**Path 2 — Firmware extraction from official dongle (medium, days):**
Pop the Flydigi USB dongle apart, identify the nRF52 (likely nRF52810), check if
APPROTECT is set. If not, dump firmware via SWD with a J-Link or Black Magic Probe
and locate the AES key + protocol parser. Then re-implement the receiver. This is
the most reliable path but requires physical disassembly + SWD wiring.

**Path 3 — Firmware extraction from controller (harder):**
Same idea but inside the controller itself. Less attractive — controller has
sticks/buttons in the way and is much more annoying to open without damage.

**Recommended:** Try Path 1 first (it's just code), then Path 2 if needed.

### Path 1 result (2026-06-03): FAILED
`dewhiten_test.py` ran Nordic-style 7-bit LFSR de-whitening with seeds:
ch41|0x40 (BLE), ch41, 0x55, addr[0]=0x66, addr[4]=0xee, per-packet byte9 counter.
For every seed, all 15 input-state labels produced 15 distinct mode-tuples — i.e.,
de-whitened distributions remain statistically identical across input states.
This rules out simple LFSR whitening with any static or counter seed.

**Confidence: HIGH** that the payload is AES-encrypted (likely AES-128-CCM, the
standard nRF52 SoftDevice / Gazell Link Layer choice). Without the key, raw
payload bytes are indistinguishable from random.

### Path 2 plan
1. Disassemble official Flydigi USB dongle (small SMT pads expected for SWD)
2. Identify the nRF52 part number
3. Probe SWDIO/SWDCLK with multimeter/oscilloscope or trace from package pins
4. Connect SWD programmer (J-Link Mini, Black Magic Probe, or DAPLink)
5. Read APPROTECT register — if locked, the project is much harder (chip swap or
   side-channel needed); if open, dump flash via `nrfjprog --readcode`
6. Reverse engineer the radio callback / packet handler in Ghidra to extract:
   - AES key material (likely a fixed key in flash, or derived from pairing data)
   - Nonce construction (counter + address?)
   - Payload structure (button bitmap, stick axis layout)
7. Implement decryption + parser on nice!nano receiver
8. Add USB composite XInput device on the receiver dongle

Until Path 2 completes, no further progress is possible from sniffing alone.

---

## Space Station installer analysis (2026-06-03)

Goal: find firmware bundled in or downloaded by Flydigi's PC updater
(Space Station) so we can avoid SWD wiring.

Downloaded `FlydigiSpaceStation_setup_3.4.4.3_Release.exe` (101 MB) from
`https://tencent-android.cdn.flydigi.com/PC/Space%20Station3.0/FlydigiSpaceStation_setup_3.4.4.3_Release.exe`.
Inno Setup 6.3 — needed innoextract HEAD (`brew install --HEAD innoextract`)
since the released 1.9 version chokes on 6.3.

Extracted to `/tmp/flydigi-rev/inno-out/` (982 files, 233 MB).

### What's in the installer
- `app/FlydigiSpaceStation.dll` (49 MB) — main .NET assembly, contains URLs
  encrypted/obfuscated; plain `strings` finds only `https://www.flydigi.com/download`
- `app/esptool.exe` — Espressif flasher (some Flydigi products are ESP32)
- `app/WCH55xISPDLL.dll`, `CH375DLL64.dll` — WCH MCU programming
- `app/tool/BurnTool.exe` (Qt5) — generic chip burner supporting **many** chip
  families: GD32VF103 (RISC-V), and codenames NB1X, SWAN, Sparta, DW31, BS21,
  BS25, Melody, ws53, Uranus (HiSilicon/BES Bluetooth family). Config files in
  `app/tool/configure/config_chip_type_*.ini` enumerate all of them.
- `app/default/device_f4/*.fdg` — Vader 4 *button-mapping* configs, **not firmware**
- `app/Service/` — Windows driver/service files (hidvirtualdriver, gamemap, etc.)

### Critical finding: NO firmware is bundled
- Searched all 982 extracted files for `.hex`/`.bin`/`.dfu`/`.fwpkg` — none found
  that resemble device firmware (only `unins000.dat`, an `images.zip` of UI art,
  and a tiny `installer.exe`).
- Conclusion: **Space Station downloads firmware on demand from a CDN**. The URL
  is embedded somewhere in the obfuscated .NET assembly; static extraction with
  `strings` can't find it.

### Why we can't ID the chip from the installer
The mix of `esptool.exe` + WCH ISP + BurnTool's 15+ supported codenames means
the Vader 4 dongle could be any of: ESP32, WCH CH55x, GD32VF103, HiSilicon
BS21/BS25, an ASR/BES Bluetooth chip — or the Nordic nRF52 the FCC filing
claimed. **No positive evidence narrows it down.**

### Three remaining paths to firmware

**Path A (cheapest, most informative): physical inspection.**
Open the official Flydigi USB dongle case. Photograph the largest chip's
markings. Five minutes of work, settles the chip ID and reveals SWD pads in
one shot.

**Path B: Windows VM + network sniff.**
Run Space Station inside a Windows VM with Wireshark capturing on the host.
Trigger a firmware-version check / forced update. The HTTPS request to the
firmware CDN exposes the URL pattern + the firmware blob. From the blob's
structure (Intel HEX vs ELF vs raw vs `.fwpkg` container) we ID the chip
and get the actual code to RE.

**Path C: .NET decompile.**
Install .NET SDK (`brew install --cask dotnet-sdk`), `dotnet tool install
-g ilspycmd`, decompile `FlydigiSpaceStation.dll`, search for firmware
download URL construction. Lower information value than Path B because we'd
still need to fetch the firmware and figure out its structure.

Recommended: **Path A**, then Path B if Path A doesn't reveal an obvious
flash-readable bootloader.

### Useful artifacts saved
- `/tmp/flydigi-rev/SpaceStation.exe` — original installer
- `/tmp/flydigi-rev/inno-out/` — fully extracted tree
- `/tmp/flydigi-rev/inno-out/app/tool/configure/` — chip-type INI files
  enumerating every chip Flydigi's manufacturing flasher supports

---

## OLD: pre-address scratch notes (kept for reference)

### Stale situation (no longer applies — address now known)
wsniff is capturing promiscuous raw packets on the active channels but CRC never validates because we don't know the real ESB address.

Raw packets captured so far (all CRC invalid, promiscuous mode):
- ch=29 rssi=-47: `02aaf400`
- ch=39 rssi=-56: `02aa4700`
- ch=55 rssi=-58: various 0xAA/0x55 pattern bytes

The WHAD `wsniff esb` raw bytes include ButteRFly's own packet metadata header, NOT just the raw radio bytes. The structure from `packet.cpp`:
  `[type(1)][timestamp(4)][source(1)][channel(1)][rssi(1)][crc_valid(1)][packet_bytes...]`

### What to try next

**Option A: Use WHAD's ESB scanning mode properly**
```bash
wsniff -i uart0 --force-stdout --format show esb --scanning
```
The `--scanning` flag puts ButteRFly in scan mode to auto-detect addresses. May need longer dwell.

**Option B: Use WHAD Python API to sniff with address extraction**
The WHAD ESBController has a "follow" mode that extracts addresses from promiscuous captures. Look at `esbcontroller.cpp:setFollowConfiguration()` and `buffer[3]==0x1f && buffer[4]==0x01` logic — this is how WHAD auto-detects addresses in promiscuous mode.

The key Python classes:
```python
from whad.device import WhadDevice
from whad.esb import Sniffer, Scanner
dev = WhadDevice.create('uart0')
scanner = Scanner(dev)  # try Scanner instead of Sniffer
```

**Option C: Use wuni-scan (Unifying scanner)**
Flydigi might be close enough to Logitech Unifying that `wuni-scan` can find it:
```bash
wuni-scan -i uart0
```

**Option D: Raw serial capture + manual CRC brute-force**
Capture 30+ seconds of raw serial data from `/dev/cu.usbmodemXXXXXXXX` using pyserial, then offline-process the raw bytes to find 5-byte sequences with valid CRC-16/CCITT (init=0xFFFF or 0x1D0F). The Flydigi address will be the 5-byte sequence that appears repeatedly with valid CRC.

**Option E: Flash our original ESB scanner sketch back temporarily**
The Arduino ESB scanner (at `esb_scanner/esb_scanner.ino`) shows more detail including which CRC mode hit. Might help narrow down the address.

### Reference: WHAD ESB promiscuous auto-address detection
In `esbcontroller.cpp` around line 600-610:
```cpp
if (buffer[3] == 0x1f && buffer[4] == 0x01) {
    this->setFilter(buffer[5], buffer[6], buffer[7], buffer[8], buffer[9]);
    this->setFollowConfiguration(this->filter.bytes);
}
```
This is the "automatic address detection" trigger in ButteRFly's ESB controller. When a packet with magic bytes `0x1F 0x01` is received at offset 3-4, bytes 5-9 are used as the 5-byte ESB address to lock onto. This is how WHAD auto-extracts addresses.

The WHAD Python `Scanner` class (from `whad.esb import Scanner`) likely sends this trigger and interprets the auto-detected address.

---

## File locations (June 2026 session; /tmp paths are long gone)
- `/tmp/butterfly/` — ButteRFly source with nice!nano patches
- `firmware/legacy/butterfly-nicenano-fwupgrade.uf2` — built UF2 firmware (kept locally, not in git)
- `/tmp/butterfly/dist/butterfly-nicenano-fwupgrade.uf2` — same file, may be gone after reboot
- `/tmp/.sdks/nRF5_SDK_17.1.0_ddde560/` — nRF5 SDK (with patches)
- `/tmp/arm-gnu-toolchain-15.2.rel1-darwin-arm64-arm-none-eabi/` — ARM toolchain
- `/tmp/esb_capture.py` — Python capture script (WIP)
- `/tmp/esb_scan_channels.py` — Channel scanner script

## Hardware
- nice!nano v2 (nRF52840), Adafruit UF2 bootloader v0.7.0, SoftDevice S140 6.1.1
- Flydigi Vader 4 RE controller
- Original Flydigi USB dongle

## Goal
Extract Flydigi ESB address → implement multi-controller nRF52840 receiver → present as composite XInput USB device to PC (no per-controller dongles needed).

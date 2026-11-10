# Firmware

Board: nRF52840 with the Adafruit nRF52 bootloader. Built as `adafruit:nrf52:feather52840`. The dev board is
a Pro Micro nRF52840 (nice!nano clone): no user button, one LED. Build and flash commands are in the README.

## firmware/fd_bridge — the bridge

Files:
- `fd_bridge.ino`: radio, slots, report translation, serial console
- `xinput.cpp` / `xinput.h`: USB Xbox 360 interfaces

### Radio

- **Slots:** two controller slots on radio pipes 0 and 1.
  - Addresses are **hardcoded** in `addrs[]`, and must share the last byte (one prefix).
- **Receiving:** ESB receive on one channel (default 12).
  - Each packet's END event goes through PPI to start TIMER3, and +20 µs later TXEN fires.
  - The ack ends 192 µs after the packet ends, which matches the real dongle.
  - The interrupt handler picks the reply payload and sets `TXADDRESS = RXMATCH`, so the reply goes out on
    that controller's address.
- **Replies:**
  - report → `02 aa <r30+1> 00`
  - anything else → `02 aa <b19+1> 00` (`other_mode=1`; see protocol.md, "Reconnect handshake")
- **Channel hunting:**
  - If no slot has had a report for 1 s and nothing was heard for 300 ms, step through channels 2–80,
    25 ms each.
  - It stops on the first CRC-valid packet.
  - It doesn't yet hunt for a missing controller while another one is connected.

### USB

- VID/PID `045e:028e`, product "Flydigi Bridge".
- One Xbox 360 interface per slot (class `FF/5D/01`, IN 32 B at 1 ms, OUT 32 B), implemented as a TinyUSB app
  class driver (`usbd_app_driver_get_cb`).
- Plus the core's CDC serial port, used as the debug console.
- Reports are 20-byte Xbox 360 input reports. Rumble OUT packets (`00 08 00 L R`) are parsed into
  `xinput_rumble_*`, but **not forwarded** to the controller.
- **macOS:**
  - Apple's driver only exposes the **first** interface, so one pad is visible.
  - The first plug-in asks "Allow accessory".
- **Windows:** untested (see status.md).

### Serial console (115200, any baud)

| Command | Effect |
|---|---|
| `c<ch>` | Set the channel |
| `S` | Toggle channel hunting |
| `s` | Print stats now |
| `v<slot>` | Toggle a raw dump of reports (`R<n> …`) and other packets (`O<n> …`) |
| `p<mask>` | Enabled pipes/slots (bitmask) |
| `t<us>` | Ack delay after packet END (default 20) |
| `h<mode>` | Reply to non-report packets: `0` `02`, `1` `02 aa b19+1 00` (default), `2` none, `3` `02 aa b19 00`, `4` clock, `5` mixed |

Stats line, every 2 s:

```
# stat ch=27 bad=386 turn=204us | c1 UP rep=1964 oth=0 usb=1964 rum=0/0 | c2 UP ...
```

- `bad` = CRC failures (mostly collisions)
- `turn` = measured ack turnaround
- `rep` = reports received
- `oth` = other packets
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

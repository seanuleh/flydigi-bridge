# Wireless protocol (Vader 4 Pro ↔ dongle)

Verified 2026-11-10 against two Vader 4 Pro controllers (device id 105, fw 6.9.5.5) and their official dongles.
Evidence: files in `captures/` (see `captures/README.md`).

## Radio

- **Protocol:** Nordic Enhanced ShockBurst (ESB) at **2 Mbps**, plaintext, no whitening.
  - The chips are reportedly Telink, running a Nordic-ESB-compatible mode.
- **Packet:**
  - preamble;
  - 5-byte address;
  - **9-bit packet header**: 6-bit length, 2-bit packet ID (always 0), 1 flag bit (always 1);
  - payload;
  - CRC-16: init `FFFF`, polynomial `0x11021`, computed over address, header and payload.
- The 9-bit header means the payload is **not byte-aligned** in raw dumps. `tools/decode_esb.py` realigns it.

### Addresses

- One address per controller, set when it paired with its dongle.
- In on-air order, as raw MSB-first dumps show it:
  - red `69 cc 96 6c ee`
  - blue `66 cc 69 c6 ee`

### nRF52 register mapping

The radio sends address bits LSB-first, base bytes first, and the prefix byte **last**:

```c
rb(x)  = bit-reverse byte x
BASEn  = rb(a3)<<24 | rb(a2)<<16 | rb(a1)<<8 | rb(a0)
APn    = rb(a4)      // PREFIX0/1 byte for pipe n
```

- Pipe 0 has its own base.
- Pipes 1–7 share `BASE1`. Only two arbitrary addresses fit at once (see the plan doc §1).

### Channels

- Seen: 4/5/6, 12, 27/28, 35, 40, 56, 64, 68, 75. Hits on neighboring ±1 channels are spillover.
- A connected pair stays on one channel for long periods. There is no per-packet hopping.

## Roles

- The controller transmits and expects acks.
- The dongle listens and acks every packet with a reply payload.
- Ack timing: the ack ends **192 µs** after the controller's packet ends.

## Packet types

| From | Length | Payload | Meaning |
|---|---|---|---|
| controller | 4 | `02 aa 00 00` / `82 aa 00 00` | Search ping, ~2 kHz (it retransmits because nobody acks) |
| controller | 21 | `02 5a a5 a5 50 50 69 01 02 00 … <b19> <b20>` | Connect message, every 10 ms. b19 = counter, b20 = status (`00`, or `f0` when unhappy) |
| controller | 20/21 | `02 5a a5 <vendor reply…>` | Reply to a USB vendor command (see [vendor-commands.md](vendor-commands.md)) |
| controller | 32 | gamepad report (below) | Every 10 ms (occasionally 5 ms) |
| dongle | 1 | `02` | Real dongle's reply to pings and connect messages |
| dongle | 4 | `02 aa <n> 00` | Reply to reports: n = report[30] + 1 |

### Search

- With no dongle, the controller **stays on one channel**: it alternates bursts of pings with about 250 ms
  of connect messages.
- It does not hop to look for the dongle, so **the dongle must hunt**.
- It auto-powers-off after a while if nothing answers.

### Reconnect handshake

From `captures/handshake.txt`, real dongle:

1. The dongle acks 3 pings with `02`, and the controller stops pinging.
2. The controller sends the 21-byte connect message every 10 ms. The dongle acks each with `02`.
   After 10 of them…
3. …the controller sends reports, starting with counter `05`. The dongle acks `02 aa 06 00`, and so on.

**Our bridge differs on purpose.** Replying `02` to the connect message leaves our controllers stuck there
(b20 goes `f0`). Replying **`02 aa <b19+1> 00`** makes them proceed; this was verified on both controllers.
Why the real dongle gets away with `02` is unknown.

### Following

When the bridge changes channel, connected controllers find it again within seconds (blue went from 12 to 4).

## 32-byte gamepad report

Payload offsets:

| Byte | Content |
|---|---|
| 0–1 | `02 01` header |
| 3 | `01` D-up, `02` D-right, `04` D-down, `08` D-left, `10` A, `20` B, `40` Back, `80` X |
| 4 | `01` Y, `02` Start, `04` LB, `08` RB, `10` LT digital, `20` RT digital, `40` LS click, `80` RS click |
| 5, 6, 22 | Extra buttons: `5:08/10/20`, `6:01/08`, `22:04`. **Not yet labeled.** Likely names from the reference project: C, Z, M1–M6, Fn, Turbo, Home. One paddle also sets X (mapped in the profile) |
| 7–19 | Gyro and accelerometer (noisy int16) |
| 20–23 | Button mirror: `20` = byte 3, `21` = byte 4, `22` = stick clicks + extras, `23` = extras |
| 24 / 25 | LX / LY, `0x80` = center, **Y grows downward** |
| 26 / 27 | RX / RY, same convention |
| 28 / 29 | LT / RT analog `00`–`ff` |
| 30 | Counter: follows the dongle's ack counter |
| 31 | Status: `00` with the real dongle, `9c` with our emulator (meaning unknown) |

The button map was made by correlating radio data with macOS GameController state (`captures/corr1.txt`, `tools/correlate.py`).

## Unknowns

- **The downstream path** (dongle → controller) for vendor commands: rumble, settings, the 1000 Hz rate.
  Acks seen so far only ever carry `02 aa n 00`.
- **The pairing exchange.** See [learn-and-pairing-plan.md](learn-and-pairing-plan.md) §5.
- The meaning of report byte 31, and of status `f0` in the connect message.

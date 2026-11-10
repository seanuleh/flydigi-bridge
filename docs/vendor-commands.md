# Vendor (configuration) commands and polling rate

Flydigi's PC app (Space Station) configures the controller with 15-byte USB commands `A5 <cmd> <args…> <sum>`
sent to the dongle's interface 0 (OUT ep `0x05`). Replies come back in the 32/64-byte input report at offset 14
(`A5 <cmd> …`). Reference, written for the Apex 4 but the same command family:
[uiltonlopes/flydigi-space-station-mac](https://github.com/uiltonlopes/flydigi-space-station-mac), files
`docs/protocol.md` and `docs/spacestation4-analysis.md`.

`tools/fd_cmd.py` sends these through an official dongle (needs sudo on macOS).

## Verified on our controller

- `A5 10` device info → `a5 10 69 5c 61 36 38 55 69 04 02 00 06 …`:
  - device id `0x69` = 105 = Vader 4 Pro
  - firmware `55 69` (6.9.5.5)
- **On the radio**, the controller's replies appear as 20/21-byte packets `02 5a a5 <reply>`. The device-info reply
  was seen byte-for-byte (`captures/rate.txt`).

## Polling rate (unresolved)

- Command: `A5 50 0A <v>`, where `v` = `1` 1000 Hz, `2` 500 Hz, `4` 250 Hz, `8` 125 Hz.
- What happened when we sent `A5 50 0A 01`:
  - The controller **acknowledged it over the radio** (`5a a5 a5 50 0a 01`). The dongle didn't pass that reply
    to USB in time.
  - The rate **stayed at 100 Hz**, even after a power cycle.
- Possible causes:
  - it needs a save to flash (`A5 50 03 <id+1>`; risky, it's tied to the profile slot);
  - gyro/motion mapping caps the rate (Flydigi's app warns about this, and our reports carry live IMU data);
  - the Vader 4 Pro handles it differently.
- Safest next step: set it in the official Space Station app on Windows (the user's copy is in Chinese), then
  measure the radio rate.

## Unknown: downstream path

How the dongle delivers a command to the controller hasn't been found. The ack payloads seen so far are
always `02 aa n 00`. This blocks rumble, settings passthrough and app support through the bridge.
Approach: send a command with `fd_cmd.py` while sniffing on the pair's channel **including CRC-bad packets and all
lengths**. Also consider that the command may travel on another address or pipe.

# Captures

Radio recordings backing `docs/protocol.md` and `docs/vendor-commands.md`. Most were made with `tools/fd_host.py` + `firmware/fd_sniff`.

Line format: `<host_time_s> P <device_us> <ch> <rssi> <pipe> <crc_ok> <len> <pid> <noack> <payload_hex>`.
Promiscuous captures use `R <us> <ch> <rssi> <pipe> <34 raw bytes>`. Bridge and emulator logs use their own
`P`/`R`/`#` lines (see the firmware header comments).

| File | What |
|---|---|
| `handshake.txt` | Red searching on ch 4, then its official dongle plugged in: full reconnect handshake at µs resolution |
| `replug.txt` | Official dongle replugged while the controller searched; first view of the 21-byte message and 1-byte ack |
| `unplug.txt` | Official dongle unplugged: controller switches to ~2 kHz search pings |
| `ch28.txt` | Fixed-channel capture: report → ack pairs with the official dongle |
| `survey.txt` | All-channel survey with the correct address |
| `emu1.txt` | First successful connection to `fd_dongle` (emulated dongle) |
| `buttons.txt` | Button presses streamed through the emulator |
| `corr1.txt` | Radio reports + macOS GameController state together, used to build the button map |
| `rate.txt` | During the `A5 50 0A 01` (1000 Hz) command: controller's vendor replies on the radio |
| `prom1.txt`, `prom2.txt` | Promiscuous sweeps used to find red's and blue's addresses |
| `whad-2026-06/` | June 2026 WHAD/ButteRFly logs (raw on-air bytes, before the 9-bit header was understood) |

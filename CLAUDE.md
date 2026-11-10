# CLAUDE.md

nRF52840 firmware that replaces Flydigi Vader 4 Pro 2.4 GHz dongles; one board serves several controllers as USB
Xbox 360 pads. Layout: `firmware/` (Arduino sketches), `tools/` (host scripts), `docs/`, `captures/` (radio logs).

## Docs index: read only what the task needs

| Doc | Read when |
|---|---|
| [docs/status.md](docs/status.md) | Starting any session: what works, known issues, the next step |
| [docs/protocol.md](docs/protocol.md) | Touching radio code: packets, handshake, report layout, nRF52 address mapping |
| [docs/firmware.md](docs/firmware.md) | Changing or flashing firmware: architecture, serial commands, USB |
| [docs/tools.md](docs/tools.md) | Sniffing, capturing, finding addresses, mapping buttons |
| [docs/vendor-commands.md](docs/vendor-commands.md) | Settings, polling rate, rumble, Space Station / `A5` commands |
| [docs/learn-and-pairing-plan.md](docs/learn-and-pairing-plan.md) | Learn mode, pairing, web UI, slot storage, hardware choices |
| [docs/history-2026-06.md](docs/history-2026-06.md) | Only if you need June-era context (has wrong conclusions) |
| [captures/README.md](captures/README.md) | Looking for evidence behind a protocol claim |

## Rules

- **Keep this index current.** When you add, rename or remove a doc, update the table in the same change.
  One row per doc; keep rows short.
- Put facts in the matching doc, not here. Update `docs/status.md` when something starts or stops working.
- Prefer small focused docs over growing one big file. Mark anything unverified as such.
- Build target: `arduino-cli … -b adafruit:nrf52:feather52840`. Flashing takes over the board; the user's
  controllers drop until the bridge firmware is back.
- The user does the physical steps (power, plugging dongles, pressing buttons). Give short, explicit cues,
  and start captures *before* asking them to act.

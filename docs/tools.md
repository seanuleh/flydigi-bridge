# Host tools (`tools/`)

Python 3 with `pyserial` and `pyusb` (with libusb). macOS for the Swift and sudo tools. Each tool uses the first
`/dev/cu.usbmodem*` port it finds.

| Tool | Use |
|---|---|
| `fd_host.py SECS OUT [cmd…]` | Open the board's serial port, send the commands, log every line to `OUT` for `SECS` seconds, and echo `#` lines |
| `decode_esb.py LOG` | Decode raw WHAD-style dumps (`aa`+addr+…): realign the 9-bit header, check the CRC |
| `find_addr.py CAP` | Promiscuous capture → most common 5-byte heads at every bit offset. The real address shows up as the top entry. Also counts hits on a target address hardcoded in the script |
| `fd_cmd.py info\|rate <hz>\|raw <hex>` | Send Flydigi vendor commands (`A5 …`) through an **official dongle**. Needs `sudo` in a real terminal: it detaches Apple's Xbox driver, then reattaches it |
| `gc_dump.swift` | `swiftc -O -o tools/gc_dump tools/gc_dump.swift`, then `tools/gc_dump SECS` prints `G <t> name=value…` whenever a macOS game controller changes |
| `correlate_capture.py SECS OUT` | Runs `gc_dump` and fd_sniff together. Map buttons by pressing everything once |
| `correlate.py OUT` | Matches each radio report bit to a GameController input (overlap score), and samples stick/trigger bytes |
| `xinput_watch.py SECS OUT` | **Windows.** Logs XInput pads: `PRESS` per button (tell the pads apart), `QUIET`/`ACTIVE` when a pad stops and starts changing (a radio drop; the bridge's pads never disconnect at USB level), `GAP` after PC sleep, and a per-minute update count (about 6000 means every report arrived) |
| `legacy/whad/*` | June 2026 WHAD/ButteRFly scripts. They need the `whad` package, and patch around its Scanner bug |

## Recipes

**Find a controller's address** (no dongle needed). Flash fd_sniff, wake the controller with its dongle unplugged, then:

```sh
python3 tools/fd_host.py 20 cap.txt a d10 m1,aa,55,00000000,00000000,1
python3 tools/find_addr.py cap.txt
```

**Find the channel a controller is searching on:**

```sh
python3 tools/fd_host.py 8 cap.txt x<addr> a d10 h200
```

Then count lines per channel.

**Record a handshake:** park the sniffer on the search channel with `x<addr> l<ch> d0`, then plug in the official dongle.

**Watch the bridge:** `python3 tools/fd_host.py 10 log.txt` (stats), and add `v1` for raw reports from slot 1.

Notes:
- `fd_host.py` writes to its log file buffered. Read the file after the run ends.
- Run long captures in the background, and give the user a clear "go now".

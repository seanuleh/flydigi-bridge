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

## Known issues and limits

- Controller addresses are hardcoded (learn mode is planned).
- macOS shows only the first of the two Xbox interfaces.
- **No COM port on Windows.** Because VID/PID is 045E:028E, the Xbox composite driver claims the whole
  device instead of `usbccgp`, so the CDC debug interface never enumerates. Web Serial is out on Windows
  as things stand.
- Rumble, the extra buttons and the polling rate are not supported (the downstream path is unknown).
- CRC-bad counter around 40 a second with both controllers active (collisions; retransmits hide them).
- No hunting for a second controller while the first one is connected.
- Weak links drop instead of riding out losses: a few lost reports in a row and the controller times
  out and searches again. A more tolerant timeout would help weaker setups (not needed with the cable).
- One plug-in on Windows powered the board (controllers paired) but never enumerated; a replug fixed it.
  Probably a badly seated plug; not reproduced on the retry.
- Not tested: a 30-minute-plus session, wake after hours of sleep (the user chose to skip these).

## Next step: the config channel

Done: enumeration, two pads in a gamepad tester, power-on orders, sleep and wake. COM port: none
(see Known issues).

1. Config channel for the web UI. Web Serial is out on Windows, so the leading option is a
   vendor (`FF`) interface with MS OS 2.0 descriptors (WinUSB) plus a WebUSB landing page (unverified:
   check that the Xbox composite driver leaves a WinUSB interface alone). The fallback is a separate debug
   build with CDC under a non-Microsoft VID/PID.

## Then

Learn mode, the web UI and full pairing: see [learn-and-pairing-plan.md](learn-and-pairing-plan.md) §8.

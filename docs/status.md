# Status and next steps

Last updated 2026-11-10.

## Working

- Protocol decoded ([protocol.md](protocol.md)).
- `fd_bridge` serves **two controllers at once** (red and blue) on macOS:
  - each one handshakes and streams about 100 reports a second;
  - channel hunting finds searching controllers;
  - the bridge appears as an Xbox 360 controller.

## Known issues and limits

- Controller addresses are hardcoded (learn mode is planned).
- macOS shows only the first of the two Xbox interfaces.
- Rumble, the extra buttons and the polling rate are not supported (the downstream path is unknown).
- CRC-bad counter around 40 a second with both controllers active (collisions; retransmits hide them).
- No hunting for a second controller while the first one is connected.
- Not yet tested: long sessions, wake after hours of sleep, all power-on orders.

## Next step: Windows test (HTPC)

1. Plug in the bridge with the official dongles unplugged, then turn on both controllers.
2. `joy.cpl` (or gamepad-tester.com) should show **two** Xbox 360 controllers.
3. Device Manager should show a COM port. That decides Web Serial vs WebUSB for the web UI.
4. Reliability: sleep and wake, power-on order, a 30-minute session.

If Windows binds nothing, or only one interface, try in this order:
- MS OS 1.0 descriptors with compatible ID `XUSB10` per interface;
- removing the CDC port;
- device class `FF/FF/FF` like a real 360 pad.

## Then

Learn mode, the web UI and full pairing: see [learn-and-pairing-plan.md](learn-and-pairing-plan.md) §8.

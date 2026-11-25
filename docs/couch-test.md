# Couch test on the HTPC (Windows)

Plan for measuring and tuning the HolyIoT PA dongle ([holyiot.md](holyiot.md)) where it will live: on the
Windows HTPC, controllers at the couch. Meant to be run by Claude Code on the HTPC with the user doing the
physical steps.

## Setup on the HTPC

- **Git** and this repo (`git pull` for the latest).
- **Python 3** (`winget install Python.Python.3.12`, or python.org) and **pyserial**: `py -m pip install pyserial`.
- Only if reflashing: **nrfutil** for Windows (Nordic's download page, or `winget search nrfutil`; unverified
  which works), then `nrfutil install nrf5sdk-tools`. Arduino isn't needed: build on the Mac, copy the zip.
- The dongle already runs the HolyIoT build (fw 0.2.2, TX 0 dBm, default Red/Blue slots). Its default USB mode
  is the Windows composite one: two Xbox 360 pads and a COM port.

Checks before measuring:
1. Device Manager: two "Xbox 360 Controller for Windows" pads and a COM port (note its number). If the COM
   port is missing, see [status.md](status.md), "Known issues".
2. Only one program can hold the COM port: close the web page tab first.
3. `py tools\fd_host.py 5 check.txt status`: prints `# stat` lines. If it can't find the port by name, add
   `--port COM3` (the number from step 1) before the other arguments.
4. Only one bridge powered: unplug the Pro Micro. Both answer the same controller addresses.

## Measuring

One run = 90 s of stats, then a summary:

```
py tools\fd_host.py 90 couch_blue.txt
py tools\stat_summary.py couch_blue.txt
```

`stat_summary.py` prints, per controller (`c1` = slot 0 = Red, `c2` = slot 1 = Blue): % of time linked,
reports a second (mean and worst 2 s window), retransmits a second (`dup`, high means it misses our acks),
RSSI range and link drops; plus channels used and CRC-bad packets a second (interference).

Cues for the user: say exactly where to sit and how to hold the controller, start `fd_host.py` **first**, then
tell them to pick up the controller and play/hold still for the 90 s.

**Good result:** linked ~100%, reports ≥ 95/s with no window under ~80, `dup` under 1/s, no drops.

**RSSI here reads ~13 dB high** (the LNA's gain, unverified). Against the old board's numbers subtract ~13 dB;
for comparisons between positions of this dongle, use the raw values as they are.

## Plan

1. **Baseline, Blue alone** at the couch, 90 s. Blue was the weak one (old chip antenna: −83 to −85 dBm,
   linked 12 of 60 s; wire mod: −60 to −75, linked ~75 of 90 s).
2. **Both controllers**, 90 s.
3. **Antenna angle**, once the dongle sits where it will live (on the USB extension, outside the HTPC case,
   ideally high with line of sight). Both controllers on, at the couch. 30 s per position, same seat:
   antenna vertical, horizontal pointing at the couch, horizontal across the couch, 45°. Compare mean
   reports/s first, then raw RSSI. Keep the best; repeat one 90 s run there to confirm.
4. **Interference:** if the summary shows dips with high CRC-bad counts, it's the channel, not range
   (channels 5–8 were busy at the HTPC, 68 was clean). The bridge moves by itself; note which channels it
   settles on. `ch 68` on the console parks it for a comparison run.
5. **Record** results and the chosen position/angle in [status.md](status.md) (replace the HolyIoT "Next"
   line) and commit.

If it's still weak after angle and placement: a higher-gain antenna if the stock one unscrews (SMA at the
hinge?), or a longer USB extension to bring the dongle closer to the couch.

## Reflashing on Windows (only if needed)

Build on the Mac ([holyiot.md](holyiot.md)), copy the `.zip`, then on the HTPC: hold the magnet to the dongle
until the red LED breathes, move it away, and find the new COM port ("Open DFU Bootloader"):

```
nrfutil nrf5sdk-tools dfu usb-serial -pkg holyiot.zip -p COM5
```

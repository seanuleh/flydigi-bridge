# HolyIoT 21017 nRF52840+PA dongle

USB-A stick with an external antenna and a 2401C PA/LNA front end. Arrived 2026-11-25; runs the bridge since
the same day. Vendor manual: [FCC filing](https://fccid.io/2ALGY-21017/User-Manual/User-manual-5392083.pdf).

## Hardware

- Sealed case, no buttons. **Reset is a reed switch:** hold the supplied magnet against the case until the red
  LED breathes, then move the magnet well away (manual: > 0.3 m) or it keeps resetting.
- PA/LNA: **TXEN = P0.24, RXEN = P0.22**, both active high. Source:
  [wbober/hci_usb-holyiot-21017-52840pa](https://github.com/wbober/hci_usb-holyiot-21017-52840pa) ("per
  schematic"). Not checked against a Holyiot schematic, but Blue links at full rate with them (2026-11-25).
- Green LED P1.09, active low. The bridge switches it on at boot (a running app is visible).
- TX power into the PA: 0 dBm (`FD_TXPOWER`; the Feather build uses +8). Out of the PA about +20 dBm.
- 32 kHz crystal: unknown, so the build uses the internal RC oscillator (`USE_LFRC`). The radio runs from the
  32 MHz crystal either way.

## As shipped

- USB 1915:C00A "nRF52 Connectivity": Holyiot's build of Nordic's connectivity firmware for nRF Connect for
  Desktop, with PA control. Erased by our flash; there's no download for it.
- Magnet reset gives the **Nordic Open DFU bootloader**: USB 1915:521F "Open DFU Bootloader", at 0xE0000. It
  takes SoftDevice and app updates only, so the Adafruit UF2 bootloader can't be put on over USB (the case
  hides the SWD pads).
- `adafruit-nrfutil` (Arduino upload) prints "Device programmed" against the connectivity firmware but writes
  nothing (likely: its serial link layer acks the old HCI DFU packets; unverified).
- The bootloader ignores GPREGRET 0xB1, so firmware can't send itself there: only the magnet works.

## Building and flashing

The HolyIoT build is the Feather build with its own variant
([firmware/variants/holyiot_21017](../firmware/variants/holyiot_21017)). It sets `USE_LFRC`, the LED,
`FD_FEM_TXEN`/`FD_FEM_RXEN`, `FD_NORDIC_BOOTLOADER` and `FD_FS_ADDR` (config at 0xDD000-0xE0000, the app-data
pages Nordic DFU keeps; the core's InternalFS area 0xED000 is inside the bootloader and crashed the bridge at
boot).

```sh
cd firmware/fd_bridge
arduino-cli compile -b adafruit:nrf52:feather52840 \
  --build-property build.variant.path=$PWD/../variants/holyiot_21017 --output-dir build/holyiot .
# app only (S140 6.1.1 is already on the board):
nrfutil nrf5sdk-tools pkg generate --hw-version 52 --sd-req 0xB6 \
  --application build/holyiot/fd_bridge.ino.hex --application-version 1 holyiot.zip
# magnet until the red LED breathes, then:
nrfutil nrf5sdk-tools dfu usb-serial -pkg holyiot.zip -p /dev/cu.usbmodem<...>
```

`nrfutil` is the Homebrew cask plus `nrfutil install nrf5sdk-tools`. First flash only: add the SoftDevice
(`--softdevice s140_nrf52_6.1.1_softdevice.hex --sd-id 0xB6`, from the Adafruit_nRF52_Bootloader repo,
`lib/softdevice/`) and accept any current SoftDevice in `--sd-req`.

## Measuring range

RSSI is read after the LNA, so it reads about 13 dB higher than the true signal (LNA gain from the overlay
above; unverified). Compare boards by report rate, retransmits and drops, or subtract ~13 dB.

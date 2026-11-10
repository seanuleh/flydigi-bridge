"""
Long scan + per-channel activity counter to find all addresses and
channels the Vader 4 uses. Run while actively wiggling sticks.
"""
import sys, time
from collections import Counter
from whad.device import WhadDevice
from whad.esb.connector.scanner import Scanner
from whad.esb.connector.base import ESB

# Patch WHAD bug
def _patched_init(self, device):
    ESB.__init__(self, device)
    from whad.esb.scanning import CommunicatingDevicesDB
    from whad.exceptions import UnsupportedCapability
    self._Scanner__db = CommunicatingDevicesDB()
    if not self.can_sniff():
        raise UnsupportedCapability('Sniff')
    self.stop()
    self.start_sniff(show_acknowledgements=True, address="FF:FF:FF:FF:FF")
Scanner.__init__ = _patched_init

DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 60
dev = WhadDevice.create('uart0')
scanner = Scanner(dev)
scanner.start()

print(f"[+] Long scan for {DURATION}s — wiggle sticks and press buttons throughout!")
seen = {}     # addr -> Counter(channel)
deadline = time.time() + DURATION
try:
    for device in scanner.discover_devices():
        addr = str(device.address)
        chans = getattr(device, 'channels', [])
        if addr not in seen:
            seen[addr] = Counter()
            print(f"  [NEW addr] {addr} rssi={device.rssi} first_channels={chans}")
        for c in chans:
            seen[addr][c] += 1
        if time.time() > deadline:
            break
except KeyboardInterrupt:
    pass
scanner.stop()
dev.close()

print(f"\n[+] Found {len(seen)} address(es):")
for addr, chans in seen.items():
    print(f"  {addr}  channels seen: {dict(chans)}")

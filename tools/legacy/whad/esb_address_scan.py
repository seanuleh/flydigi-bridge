"""
Use WHAD Scanner to auto-discover Flydigi Vader 4 ESB address.

Scanner sweeps channels and yields devices once their address is extracted
from packet bytes 5-9 when magic 0x1F 0x01 appears at bytes 3-4.
"""
import sys, time
from whad.device import WhadDevice
from whad.esb.connector.scanner import Scanner
from whad.esb.connector.base import ESB

# Monkey-patch around WHAD bug: Scanner.__init__ calls super().sniff(...)
# but base ESB has start_sniff(...), not sniff(...).
_orig_init = Scanner.__init__
def _patched_init(self, device):
    ESB.__init__(self, device)
    from whad.esb.scanning import CommunicatingDevicesDB
    self._Scanner__db = CommunicatingDevicesDB()
    if not self.can_sniff():
        from whad.exceptions import UnsupportedCapability
        raise UnsupportedCapability('Sniff')
    self.stop()
    self.start_sniff(show_acknowledgements=True, address="FF:FF:FF:FF:FF")
Scanner.__init__ = _patched_init

dev = WhadDevice.create('uart0')
scanner = Scanner(dev)
scanner.start()

print("[+] Scanning all channels for ESB devices for 30s...")
print("[+] Wiggle sticks / press buttons on the Vader 4!")
print()

deadline = time.time() + 30
seen = {}
try:
    for device in scanner.discover_devices(minimal_rssi=-80):
        addr = str(device.address)
        if addr not in seen:
            seen[addr] = device
            print(f"  [NEW] addr={addr} rssi={device.rssi} channels={getattr(device,'channels',None)}")
        else:
            seen[addr] = device
        if time.time() > deadline:
            break
except KeyboardInterrupt:
    pass

scanner.stop()
dev.close()

print(f"\n[+] Discovered {len(seen)} device(s):")
for addr, device in seen.items():
    print(f"  {addr}  rssi={device.rssi}  attrs={vars(device)}")

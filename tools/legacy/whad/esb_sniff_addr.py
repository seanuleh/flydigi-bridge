"""
Sniff ESB packets locked to a specific address and dump payloads.
Verifies that 66:cc:69:c6:ee is the Flydigi Vader 4 by watching payload
bytes change as buttons/sticks are operated.
"""
import sys, time
from whad.device import WhadDevice
from whad.esb.connector.base import ESB
from whad.esb.connector.sniffer import Sniffer
from whad.esb.sniffing import SnifferConfiguration

ADDRESS = sys.argv[1] if len(sys.argv) > 1 else "66:cc:69:c6:ee"
CHANNEL = int(sys.argv[2]) if len(sys.argv) > 2 else None  # None = scan
DURATION = int(sys.argv[3]) if len(sys.argv) > 3 else 20

dev = WhadDevice.create('uart0')
sniffer = Sniffer(dev)

cfg = SnifferConfiguration()
cfg.address = ADDRESS
cfg.show_acknowledgements = True
if CHANNEL is not None:
    cfg.channel = CHANNEL
sniffer.configuration = cfg

sniffer.start()
print(f"[+] Sniffing addr={ADDRESS} ch={CHANNEL} for {DURATION}s")
print("[+] Press buttons / wiggle sticks!")
print()

deadline = time.time() + DURATION
count = 0
try:
    for pkt in sniffer.sniff():
        count += 1
        m = pkt.metadata
        raw = bytes(pkt)
        print(f"  [{count:4d}] ch={m.channel:3} rssi={m.rssi:4} crc={m.is_crc_valid} len={len(raw):2} {raw.hex()}")
        if time.time() > deadline:
            break
except KeyboardInterrupt:
    pass

sniffer.stop()
dev.close()
print(f"\n[+] Total packets: {count}")

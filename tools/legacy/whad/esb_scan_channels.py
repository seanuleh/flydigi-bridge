"""
Scan each Gazell channel for ESB traffic, report packet counts and raw bytes.
Gazell uses every 5th channel: 4,9,14,19,24,29,34,39,44,49,54,59,64,69,74,79
plus some intermediaries. We also include 55 from our earlier scan.
"""
import sys, time, threading
from collections import defaultdict

CHANNELS = [4,9,14,19,24,29,34,39,44,49,54,55,59,64,69,74,79]
DWELL_SEC = 2  # seconds per channel

from whad.device import WhadDevice
from whad.esb import Sniffer

results = defaultdict(list)
current_ch = None

class MySniffer(Sniffer):
    def on_pdu(self, pdu):
        b = bytes(pdu)
        m = pdu.metadata
        results[m.channel].append((m.rssi, m.is_crc_valid, b))
        marker = "*** CRC OK ***" if m.is_crc_valid else ""
        print(f"  ch={m.channel:3} rssi={m.rssi:4} crc={m.is_crc_valid} {marker} raw={b.hex()}")

try:
    dev = WhadDevice.create('uart0')
    sniffer = MySniffer(dev)

    for ch in CHANNELS:
        print(f"\n[>] Channel {ch} ({2400+ch} MHz)...")
        sniffer.channel = ch
        sniffer.start()
        time.sleep(DWELL_SEC)
        sniffer.stop()

    dev.close()

except Exception as e:
    print(f"[!] {e}")
    import traceback; traceback.print_exc()

print("\n\n=== SUMMARY ===")
for ch in CHANNELS:
    pkts = results[ch]
    if pkts:
        ok = sum(1 for r,c,b in pkts if c)
        avg_rssi = sum(r for r,c,b in pkts) / len(pkts)
        print(f"  ch={ch:3}: {len(pkts):3} pkts, {ok} CRC-OK, avg_rssi={avg_rssi:.0f}")
    else:
        print(f"  ch={ch:3}:   0 pkts")

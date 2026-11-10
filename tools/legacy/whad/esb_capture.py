"""
Capture raw ESB packets on channel 55 and extract the Flydigi ESB address.
"""
import sys, time, struct
from collections import Counter

def crc16_ccitt(data: bytes, init: int) -> int:
    crc = init
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
        crc &= 0xFFFF
    return crc

from whad.device import WhadDevice
from whad.esb import Sniffer

packets_raw = []
packets_meta = []

class MySniffer(Sniffer):
    def on_pdu(self, pdu):
        b = bytes(pdu)
        m = pdu.metadata
        packets_raw.append(b)
        packets_meta.append(m)
        print(f"  [{len(packets_raw):3d}] ch={m.channel:3} rssi={m.rssi:4} crc={m.is_crc_valid} raw={b.hex()}")

try:
    dev = WhadDevice.create('uart0')
    sniffer = MySniffer(dev)
    sniffer.channel = 55
    sniffer.start()
    print("[+] Sniffing ch55 for 15s — wiggle sticks and press buttons!")
    time.sleep(15)
    sniffer.stop()
    dev.close()
except Exception as e:
    print(f"[!] {e}")
    import traceback; traceback.print_exc()

print(f"\n[+] Total packets captured: {len(packets_raw)}")
if not packets_raw:
    print("[!] No packets — is the controller on and dongle plugged in?")
    sys.exit(1)

# Count repeated 5-byte sequences as address candidates
addr_candidates = Counter()
for raw in packets_raw:
    for start in range(min(len(raw)-7, 4)):
        seq = bytes(raw[start:start+5])
        addr_candidates[seq] += 1

print("\n[+] Top address candidates (most repeated 5-byte sequences):")
for addr, count in addr_candidates.most_common(10):
    print(f"  {addr.hex(':')}  x{count}")
    for raw in packets_raw[:20]:
        offset = bytes(raw).find(addr)
        if offset < 0:
            continue
        rest = bytes(raw)[offset+5:]
        if len(rest) < 3:
            continue
        plen = (rest[0] >> 2) & 0x3F
        if plen == 0 or plen > 32 or len(rest) < 1 + plen + 2:
            continue
        data = bytes(raw)[offset:offset+6+plen]
        crc_got = struct.unpack('>H', bytes(rest[1+plen:1+plen+2]))[0]
        for init, name in [(0xFFFF,'ESB'),(0x1D0F,'GAZ')]:
            crc_calc = crc16_ccitt(data, init)
            if crc_calc == crc_got:
                print(f"    *** CRC MATCH ({name})! address={addr.hex(':')} payload={rest[1:1+plen].hex()}")

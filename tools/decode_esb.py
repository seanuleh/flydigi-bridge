"""Decode ButteRFly raw ESB packets (aa + 5B addr + 9-bit PCF + payload + CRC16)."""
import sys, re, collections

def crc16(bits):
    crc = 0xFFFF
    for b in bits:
        crc ^= (b << 15)
        crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
        crc &= 0xFFFF
    return crc

def decode(hexs):
    raw = bytes.fromhex(hexs)
    addr = raw[1:6]
    bits = [(byte >> (7 - i)) & 1 for byte in raw[6:] for i in range(8)]
    pcf = bits[:9]
    ln = int(''.join(map(str, pcf[:6])), 2)
    pid = int(''.join(map(str, pcf[6:8])), 2)
    noack = pcf[8]
    pl_bits = bits[9:9 + ln * 8]
    payload = bytes(int(''.join(map(str, pl_bits[i:i+8])), 2) for i in range(0, len(pl_bits), 8))
    crc_bits = bits[9 + ln*8: 9 + ln*8 + 16]
    crc_rx = int(''.join(map(str, crc_bits)), 2) if len(crc_bits) == 16 else None
    addr_bits = [(byte >> (7 - i)) & 1 for byte in addr for i in range(8)]
    ok = crc16(addr_bits + bits[:9 + ln*8]) == crc_rx
    return addr.hex(), ln, pid, noack, payload, ok

if __name__ == '__main__':
    for line in open(sys.argv[1]):
        m = re.search(r'^\s*([\d.]+)\s+(?:(\S+)\s+)?rssi=\s*(-?\d+).*? ([0-9a-f]{20,})$', line)
        if not m: continue
        t, label, rssi, hx = m.groups()
        a, ln, pid, na, pl, ok = decode(hx)
        print(f"{t:>8} {label or '':14} rssi={rssi} len={ln:2} pid={pid} noack={na} crc={'OK' if ok else 'BAD'} {pl.hex(' ')}")

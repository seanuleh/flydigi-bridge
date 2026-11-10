"""
Apply Nordic-style 7-bit whitening LFSR to captured payload bytes and
test whether de-whitened bytes correlate with input state.

Nordic BLE whitening: 7-bit LFSR, poly x^7 + x^4 + 1, init = channel | 0x40.
ESB doesn't standardly whiten, but Gazell / Flydigi might use the same scheme.

We also try a few seed variants (channel, channel|0x40, channel XOR addr bytes).
"""
from collections import Counter, defaultdict

def load(path):
    segs = defaultdict(list)
    for l in open(path):
        l = l.strip()
        if not l or l.startswith('#'): continue
        parts = l.split()
        if len(parts) < 4: continue
        try:
            label = parts[1]
            b = bytes.fromhex(parts[-1])
            if len(b) == 14:
                segs[label].append(b)
        except Exception:
            continue
    return segs

def lfsr_stream(seed, nbits):
    # Nordic BLE 7-bit LFSR, poly x^7+x^4+1
    # Implemented MSB-first
    reg = seed & 0x7F
    out = bytearray()
    byte = 0
    for i in range(nbits):
        # tap bits 7 and 4 (1-indexed) → indices 6 and 3 in our 7-bit reg
        bit = (reg >> 6) & 1
        new = bit ^ ((reg >> 2) & 1)  # x^7 XOR x^4 in 7-bit form
        reg = ((reg << 1) & 0x7E) | new
        byte = (byte << 1) | bit
        if (i & 7) == 7:
            out.append(byte & 0xFF)
            byte = 0
    if nbits & 7:
        out.append((byte << (8 - (nbits & 7))) & 0xFF)
    return bytes(out)

def dewhiten(payload, seed):
    ks = lfsr_stream(seed, len(payload)*8)
    return bytes(b ^ k for b, k in zip(payload, ks))

segs = load('active.log')
labels = ['idle','hold_A','hold_B','hold_X','hold_Y','lstick_up','lstick_down',
          'lstick_left','lstick_right','rstick_up','rstick_down','lt_full','rt_full',
          'dpad_up','idle_end']

# We don't know which channel each packet was on (capture was on single ch 41
# per sniffer config — but the sniffer follows hopping). For initial test,
# assume all packets were on ch 41 (or try several seeds).

print("Distribution of de-whitened payload (bytes 8-13) modes per label:")
for seed_name, seed in [
    ("ch41|0x40 (BLE-style)", 41 | 0x40),
    ("ch41", 41),
    ("0x55", 0x55),
    ("0x66 (addr[0])", 0x66),
    ("0xee (addr[4])", 0xee),
]:
    print(f"\n--- seed = {seed_name} = 0x{seed:02x} ---")
    print(f"{'label':14}  b08 b09 b10 b11 b12 b13")
    diffs = set()
    for lbl in labels:
        pkts = segs.get(lbl, [])
        if not pkts: continue
        dew = [dewhiten(p[8:14], seed) for p in pkts]
        modes = []
        for i in range(6):
            c = Counter(d[i] for d in dew)
            modes.append(c.most_common(1)[0][0])
        diffs.add(tuple(modes))
        print(f"{lbl:14}  " + " ".join(f"{m:02x}" for m in modes))
    print(f" -> {len(diffs)} distinct mode-tuples across labels")

# Also: try counter-based whitening (per-packet seed = b9 of packet)
print("\n--- seed = byte 9 (per-packet counter) ---")
print(f"{'label':14}  b08 b10 b11 b12 b13 (b09 is the counter, excluded)")
diffs = set()
for lbl in labels:
    pkts = segs.get(lbl, [])
    if not pkts: continue
    dew = [dewhiten(bytes([p[8]])+p[10:14], p[9]) for p in pkts]
    modes = []
    for i in range(5):
        c = Counter(d[i] for d in dew)
        modes.append(c.most_common(1)[0][0])
    diffs.add(tuple(modes))
    print(f"{lbl:14}  " + " ".join(f"{m:02x}" for m in modes))
print(f" -> {len(diffs)} distinct mode-tuples across labels")

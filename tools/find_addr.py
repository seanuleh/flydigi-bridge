import sys, collections
def bits_of(b, rev):
    return [((x >> i) & 1) if rev else ((x >> (7 - i)) & 1) for x in b for i in range(8)]
def to_bytes(bits):
    return bytes(int(''.join(map(str, bits[i:i+8])), 2) for i in range(0, len(bits) - 7, 8))
target = bytes.fromhex('66cc69c6ee')
cnt = collections.Counter(); hits = collections.Counter()
for line in open(sys.argv[1]):
    f = line.split()
    if len(f) < 7 or f[1] != 'R': continue
    raw = bytes.fromhex(f[6]); ch = f[3]
    for rev in (0, 1):
        bits = bits_of(raw, rev)
        for off in range(0, 24):
            by = to_bytes(bits[off:])
            if target in by: hits[(ch, rev, off)] += 1
            cnt[(by[:5].hex(), rev, off)] += 1
print("target hits:", hits.most_common(10))
print("most common 5-byte heads:")
for k, v in cnt.most_common(15): print(v, k)

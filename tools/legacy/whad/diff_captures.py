"""
Compare per-byte value distributions across capture logs to find which
bytes encode which inputs. Usage: diff_captures.py idle.log active.log
"""
import sys
from collections import Counter

def load(path):
    pkts = []
    for l in open(path):
        l = l.strip()
        if not l or l.startswith('#'): continue
        parts = l.split()
        if len(parts) < 4: continue
        hexstr = parts[-1]
        try:
            pkts.append(bytes.fromhex(hexstr))
        except ValueError:
            continue
    return pkts

def summarize(name, pkts):
    if not pkts:
        print(f"{name}: 0 packets"); return None
    L = len(pkts[0])
    cols = [Counter(p[i] for p in pkts if len(p)==L) for i in range(L)]
    print(f"{name}: {len(pkts)} packets, len={L}")
    return cols

if len(sys.argv) < 3:
    print("usage: diff_captures.py file1 file2 [file3 ...]")
    sys.exit(1)

cap_cols = []
for path in sys.argv[1:]:
    pkts = load(path)
    cols = summarize(path, pkts)
    cap_cols.append((path, cols))

L = max(len(c[1]) for c in cap_cols if c[1])
print(f"\n{'byte':>4} | " + " | ".join(f"{p:^32}" for p,_ in cap_cols))
for i in range(L):
    cells = []
    for _, cols in cap_cols:
        if cols and i < len(cols):
            top = cols[i].most_common(4)
            cell = " ".join(f"{v:02x}:{c}" for v,c in top)
            cells.append(f"{cell:32}")
        else:
            cells.append(" " * 32)
    print(f"{i:>4} | " + " | ".join(cells))

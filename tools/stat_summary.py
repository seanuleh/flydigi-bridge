"""Summarise an fd_host.py log of fd_bridge stats lines (the couch test).
Usage: stat_summary.py LOG [LOG ...]
Per controller: time linked, reports a second (mean / min over 2 s windows), retransmits a second (dup),
RSSI range, link drops. Also the channels used and CRC-bad packets a second."""
import re, sys

STAT = re.compile(r'^([\d.]+) # stat ch=(\d+) bad=(\d+) .*?\| (.*)$')
SLOT = re.compile(r'c(\d) (\w+) P\d rep=(\d+) dup=(\d+) oth=\d+ usb=\d rssi=(-?\d+)')

def summarise(path):
    rows = []
    for line in open(path, errors='replace'):
        m = STAT.match(line.strip())
        if m: rows.append((float(m[1]), int(m[2]), int(m[3]), SLOT.findall(m[4])))
    if len(rows) < 2: print(f"{path}: fewer than 2 stats lines"); return
    span = rows[-1][0] - rows[0][0]
    chans = []
    for _, ch, _, _ in rows:
        if not chans or chans[-1] != ch: chans.append(ch)
    bad = [(b2 - b1) / (t2 - t1) for (t1, _, b1, _), (t2, _, b2, _) in zip(rows, rows[1:]) if b2 >= b1 and t2 > t1]
    print(f"{path}: {span:.0f} s, channels {' > '.join(map(str, chans))}, "
          f"CRC-bad {sum(bad) / len(bad):.0f}/s (max {max(bad):.0f})" if bad else f"{path}: {span:.0f} s")
    for c in sorted({s[0] for r in rows for s in r[3]}):
        up_t, rates, dups, rssi, drops, prev = 0.0, [], [], [], 0, None
        for t, _, _, slots in rows:
            s = next((x for x in slots if x[0] == c), None)
            if not s: continue
            up, rep, dup, r = s[1] == 'UP', int(s[2]), int(s[3]), int(s[4])
            if prev:
                pt, pup, prep, pdup = prev
                if pup and not up: drops += 1
                if pup and up and rep >= prep and t > pt:
                    up_t += t - pt
                    rates.append((rep - prep) / (t - pt)); dups.append((dup - pdup) / (t - pt))
            if up and r: rssi.append(r)
            prev = (t, up, rep, dup)
        if not rates: print(f"  c{c}: never linked"); continue
        print(f"  c{c}: linked {100 * up_t / span:.0f}% | reports {sum(rates) / len(rates):.0f}/s "
              f"(min {min(rates):.0f}) | dup {sum(dups) / len(dups):.1f}/s | "
              f"RSSI {min(rssi)} to {max(rssi)} | drops {drops}")

for p in sys.argv[1:]: summarise(p)

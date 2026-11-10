"""Drive fd_sniff firmware. Usage: fd_host.py SECONDS OUTFILE [cmd ...]"""
import sys, time, serial, glob
port = sorted(glob.glob('/dev/cu.usbmodem*'))[0]
dur, out, cmds = float(sys.argv[1]), sys.argv[2], sys.argv[3:]
s = serial.Serial(port, 115200, timeout=0.1)
time.sleep(0.3); s.reset_input_buffer()
for c in cmds:
    s.write((c + '\n').encode()); time.sleep(0.05)
t0 = time.time(); n = 0
with open(out, 'w') as f:
    while time.time() - t0 < dur:
        line = s.readline().decode(errors='replace').strip()
        if not line: continue
        if line.startswith('#'): print(line)
        f.write(f"{time.time()-t0:.4f} {line}\n"); n += 1
print(f"{n} lines -> {out}")

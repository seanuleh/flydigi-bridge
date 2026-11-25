"""Drive fd_sniff / fd_bridge firmware. Usage: fd_host.py [--port PORT] SECONDS OUTFILE [cmd ...]
Without --port: the first port whose USB product is "Flydigi Bridge", else the first /dev/cu.usbmodem*.
Works on Windows too (pass --port COM3 if the bridge isn't found by name)."""
import sys, time, serial, glob
from serial.tools import list_ports
args = sys.argv[1:]
if args[:1] == ['--port']: port, args = args[1], args[2:]
else:
    named = [p.device for p in list_ports.comports() if 'Flydigi' in (p.product or '') + (p.description or '')]
    port = (named or sorted(glob.glob('/dev/cu.usbmodem*')) or [None])[0]
    if not port: sys.exit("no bridge port found; pass --port")
dur, out, cmds = float(args[0]), args[1], args[2:]
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

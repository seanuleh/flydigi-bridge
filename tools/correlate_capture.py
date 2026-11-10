"""Simultaneously record official dongle XInput reports (USB) + radio reports (fd_sniff)."""
import os, sys, time, threading, glob, serial
DUR = float(sys.argv[1]); OUT = sys.argv[2]
lines = []; lock = threading.Lock(); t0 = time.time(); stop = False

def usb_reader():
    import subprocess
    p = subprocess.Popen([os.path.join(os.path.dirname(os.path.abspath(__file__)), "gc_dump"), str(DUR + 1)], stdout=subprocess.PIPE, text=True)
    for l in p.stdout:
        l = l.strip()
        if stop: break
        if l.startswith('G '):
            with lock: lines.append(f"{time.time()-t0:.4f} {l}")
        else: print(l)
    p.kill()

def radio_reader():
    s = serial.Serial(sorted(glob.glob('/dev/cu.usbmodem*'))[0], 115200, timeout=0.1)
    time.sleep(0.3); s.reset_input_buffer()
    for c in ["l4,5,12,28,35,40,56,64,68,75", "d3", "h500"]: s.write((c + "\n").encode()); time.sleep(0.05)
    while not stop:
        l = s.readline().decode(errors='replace').strip()
        if l.startswith('P '):
            with lock: lines.append(f"{time.time()-t0:.4f} {l}")

ts = [threading.Thread(target=usb_reader, daemon=True), threading.Thread(target=radio_reader, daemon=True)]
for t in ts: t.start()
time.sleep(DUR); stop = True; time.sleep(0.5)
with open(OUT, 'w') as f: f.write("\n".join(lines) + "\n")
print(f"{sum(1 for l in lines if ' G ' in l)} usb, {sum(1 for l in lines if ' P ' in l)} radio -> {OUT}")

"""Send Flydigi vendor commands through the official dongle (XInput iface 0, OUT ep 0x05 / IN ep 0x81).
Needs sudo on macOS (detaches Apple's Xbox driver for a moment, then reattaches).
Usage: sudo python3 fd_cmd.py info | rate <1000|500|250|125> | raw <hex bytes>"""
import sys, time, usb.core, usb.util
RATES = {1000: 1, 500: 2, 250: 4, 125: 8}

def frame(*b):
    p = bytes(b) + bytes(15 - len(b))
    return p[:14] + bytes([sum(p[:14]) & 0xFF])

def xfer(dev, pkt, want, tries=4, wait=0.6):
    for _ in range(tries):
        dev.write(0x05, pkt, timeout=500)
        t = time.time()
        while time.time() - t < wait:
            try: r = bytes(dev.read(0x81, 64, timeout=100))
            except usb.core.USBTimeoutError: continue
            if len(r) > 16 and r[14] == 0xA5 and r[15] == want: return r
    return None

dev = usb.core.find(idVendor=0x045e, idProduct=0x028e)
if not dev: sys.exit("dongle (045e:028e) not found")
reattach = False
try:
    if dev.is_kernel_driver_active(0): dev.detach_kernel_driver(0); reattach = True
except Exception as e: print("detach:", e)
usb.util.claim_interface(dev, 0)
try:
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'info'
    r = xfer(dev, frame(0xA5, 0x10, 0x00), 0x10)
    if r: print(f"device id={r[16]} fw={r[21]:02x}{r[22]:02x} batt={r[23]} chip={r[24]} conn={r[25]} motion={r[26]}  raw={r[14:30].hex(' ')}")
    else: print("no reply to device-info (A5 10) — is the controller on?")
    if cmd == 'rate':
        v = RATES[int(sys.argv[2])]
        r = xfer(dev, frame(0xA5, 0x50, 0x0A, v), 0x50)
        print("rate ack:", r[14:30].hex(' ') if r else "none")
    elif cmd == 'raw':
        b = bytes.fromhex(''.join(sys.argv[2:]))
        r = xfer(dev, frame(*b), b[1])
        print("reply:", r[14:40].hex(' ') if r else "none")
finally:
    usb.util.release_interface(dev, 0)
    if reattach:
        try: dev.attach_kernel_driver(0)
        except Exception as e: print("reattach:", e, "(unplug/replug the dongle if the pad stops working)")

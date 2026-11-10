"""Windows XInput watcher for bridge reliability tests.

python tools/xinput_watch.py SECS OUT

Polls XInput slots 0-3 and writes one line per event to OUT (flushed per line):
  <t> CONNECT <slot> / DISCONNECT <slot>   slot appears or drops
  <t> PRESS <slot> <buttons>               new buttons held (use this to tell the pads apart)
  <t> QUIET <slot> / ACTIVE <slot> after Ns  no state change for QUIET_S seconds, then changes again.
      The bridge always presents both pads, so CONNECT/DISCONNECT do not show radio drops; a long
      QUIET while the controller is in use does. (A pad lying still can also go quiet: stick noise varies.)
  <t> GAP <secs>                           the script was paused (PC sleep), then resumed
  <t> MIN <slot>=<updates> ...             once a minute: state changes per connected slot
t is seconds since start. Windows only (xinput1_4.dll).
"""
import ctypes, sys, time
from ctypes import wintypes

BUTTONS = [(0x0001, "Up"), (0x0002, "Down"), (0x0004, "Left"), (0x0008, "Right"), (0x0010, "Start"),
           (0x0020, "Back"), (0x0040, "LS"), (0x0080, "RS"), (0x0100, "LB"), (0x0200, "RB"),
           (0x0400, "Guide"), (0x1000, "A"), (0x2000, "B"), (0x4000, "X"), (0x8000, "Y")]
QUIET_S = 2.0


class GAMEPAD(ctypes.Structure):
    _fields_ = [("wButtons", wintypes.WORD), ("bLeftTrigger", ctypes.c_ubyte), ("bRightTrigger", ctypes.c_ubyte),
                ("sThumbLX", ctypes.c_short), ("sThumbLY", ctypes.c_short),
                ("sThumbRX", ctypes.c_short), ("sThumbRY", ctypes.c_short)]


class STATE(ctypes.Structure):
    _fields_ = [("dwPacketNumber", wintypes.DWORD), ("Gamepad", GAMEPAD)]


def main():
    secs, out = float(sys.argv[1]), sys.argv[2]
    xi = ctypes.WinDLL("xinput1_4.dll")
    # Ordinal 100 is XInputGetStateEx: like XInputGetState but also reports the Guide button.
    get = xi[100]
    get.argtypes = [wintypes.DWORD, ctypes.POINTER(STATE)]
    ctypes.windll.winmm.timeBeginPeriod(1)
    f = open(out, "w", buffering=1)
    t0 = last = time.monotonic()
    nxt_min = t0 + 60
    conn, pkt, btn, upd = [False] * 4, [0] * 4, [0] * 4, [0] * 4
    seen, quiet = [t0] * 4, [False] * 4
    st = STATE()

    def log(msg):
        line = f"{time.monotonic() - t0:9.2f} {msg}"
        f.write(line + "\n")
        print(line, flush=True)

    log("START " + time.strftime("%Y-%m-%d %H:%M:%S"))
    while (now := time.monotonic()) - t0 < secs:
        if now - last > 2:
            log(f"GAP {now - last:.1f}")
        last = now
        for i in range(4):
            ok = get(i, ctypes.byref(st)) == 0
            if ok != conn[i]:
                conn[i] = ok
                log(("CONNECT " if ok else "DISCONNECT ") + str(i))
                btn[i] = 0
            if not ok:
                continue
            if st.dwPacketNumber != pkt[i]:
                pkt[i] = st.dwPacketNumber
                upd[i] += 1
                if quiet[i]:
                    log(f"ACTIVE {i} after {now - seen[i]:.1f}s")
                    quiet[i] = False
                seen[i] = now
            elif not quiet[i] and now - seen[i] > QUIET_S:
                log(f"QUIET {i}")
                quiet[i] = True
            b = st.Gamepad.wButtons
            new = b & ~btn[i]
            if new:
                log(f"PRESS {i} " + "+".join(n for m, n in BUTTONS if new & m))
            btn[i] = b
        if now >= nxt_min:
            log("MIN " + " ".join(f"{i}={upd[i]}" for i in range(4) if conn[i]))
            upd = [0] * 4
            nxt_min += 60
        time.sleep(0.002)
    log("END")


if __name__ == "__main__":
    main()

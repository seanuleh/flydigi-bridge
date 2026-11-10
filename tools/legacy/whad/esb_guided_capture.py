"""
Guided capture: prompts the user through a fixed sequence of actions,
tagging each segment with a label in the log. Run interactively.
"""
import sys, time, threading
from whad.device import WhadDevice
from whad.esb.connector.sniffer import Sniffer
from whad.esb.sniffing import SnifferConfiguration

ADDRESS = "66:cc:69:c6:ee"
CHANNEL = 41
LOGFILE = "active.log"

SEQUENCE = [
    ("idle",          "Set controller down, hands off. Idle baseline.",          5),
    ("hold_A",        "HOLD the A button.",                                       5),
    ("hold_B",        "HOLD the B button.",                                       5),
    ("hold_X",        "HOLD the X button.",                                       5),
    ("hold_Y",        "HOLD the Y button.",                                       5),
    ("lstick_up",     "HOLD left stick fully UP.",                                4),
    ("lstick_down",   "HOLD left stick fully DOWN.",                              4),
    ("lstick_left",   "HOLD left stick fully LEFT.",                              4),
    ("lstick_right",  "HOLD left stick fully RIGHT.",                             4),
    ("rstick_up",     "HOLD right stick fully UP.",                               4),
    ("rstick_down",   "HOLD right stick fully DOWN.",                             4),
    ("lt_full",       "HOLD left trigger (LT) fully pressed.",                    4),
    ("rt_full",       "HOLD right trigger (RT) fully pressed.",                   4),
    ("dpad_up",       "HOLD D-Pad UP.",                                           4),
    ("idle_end",      "Hands off, idle again.",                                   3),
]

dev = WhadDevice.create('uart0')
sniffer = Sniffer(dev)
cfg = SnifferConfiguration()
cfg.address = ADDRESS
cfg.channel = CHANNEL
cfg.show_acknowledgements = True
sniffer.configuration = cfg
sniffer.start()

t0 = time.monotonic()
current_label = ["startup"]
stop = [False]
fh = open(LOGFILE, 'w')
fh.write(f"# guided capture addr={ADDRESS} ch={CHANNEL}\n")

def reader():
    for pkt in sniffer.sniff():
        if stop[0]: break
        t = time.monotonic() - t0
        m = pkt.metadata
        raw = bytes(pkt).hex()
        fh.write(f"{t:7.3f} {current_label[0]:14} rssi={m.rssi:4} crc={int(m.is_crc_valid)} {raw}\n")
        fh.flush()

th = threading.Thread(target=reader, daemon=True)
th.start()

print(f"[+] Logging to {LOGFILE}")
print("[+] Follow the prompts. Each step runs for the indicated duration.\n")
for label, instr, dur in SEQUENCE:
    print(f"  >>> [{label}] {instr}  ({dur}s)")
    current_label[0] = label
    time.sleep(dur)
stop[0] = True
time.sleep(0.5)
sniffer.stop()
dev.close()
fh.close()
print(f"\n[+] Done. Wrote {LOGFILE}")

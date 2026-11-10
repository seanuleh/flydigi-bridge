"""
Capture timestamped packets to a log file for offline analysis.
Print live and write to file so we can correlate user actions
(idle, button A, stick up, etc.) with payload bytes.
"""
import sys, time, datetime
from whad.device import WhadDevice
from whad.esb.connector.sniffer import Sniffer
from whad.esb.sniffing import SnifferConfiguration

ADDRESS = "66:cc:69:c6:ee"
CHANNEL = 41
DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 30
LOGFILE = sys.argv[2] if len(sys.argv) > 2 else "capture.log"

dev = WhadDevice.create('uart0')
sniffer = Sniffer(dev)
cfg = SnifferConfiguration()
cfg.address = ADDRESS
cfg.channel = CHANNEL
cfg.show_acknowledgements = True
sniffer.configuration = cfg

sniffer.start()
print(f"[+] addr={ADDRESS} ch={CHANNEL} dur={DURATION}s → {LOGFILE}")
print("[+] Cue the user; everything is logged with monotonic timestamps.")
t0 = time.monotonic()
deadline = t0 + DURATION
n = 0
with open(LOGFILE, 'w') as f:
    f.write(f"# capture {datetime.datetime.now().isoformat()} addr={ADDRESS} ch={CHANNEL}\n")
    try:
        for pkt in sniffer.sniff():
            t = time.monotonic() - t0
            m = pkt.metadata
            raw = bytes(pkt).hex()
            line = f"{t:7.3f} rssi={m.rssi:4} crc={int(m.is_crc_valid)} {raw}"
            print(line)
            f.write(line + "\n")
            n += 1
            if time.monotonic() > deadline:
                break
    except KeyboardInterrupt:
        pass
sniffer.stop()
dev.close()
print(f"[+] {n} packets → {LOGFILE}")

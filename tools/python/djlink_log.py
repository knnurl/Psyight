"""Passive Pro DJ Link logger: records every packet the CDJs and the DJM put on the network.

Listens on UDP 50000 (announcements, keep-alives), 50001 (beats, on-air, CDJ-3000 positions) and
50002 (status, sent only to devices that join, so likely empty). It never sends anything: it
does not announce itself or join as a player, so it cannot disturb the booth.

    python tools/python/djlink_log.py [--out data/djlink] [--summary-s 2]

Writes <out>/djlink-<time>.jsonl, one packet a line:
    {"t_ns": steady clock ns, "utc": "...", "port": 50001, "src": "169.254.x.y", "hex": "..."}
t_ns is time.perf_counter_ns(), which on Windows is QueryPerformanceCounter: the same clock as
the "steady_clock_ns" column of Psyight's live recording (<recording>.clock.csv), so packets and
audio line up. Every few seconds the console prints what it has seen per device.

Packet layout (Deep Symmetry's DJ Link analysis, djl-analysis.deepsymmetry.org): every packet
starts with the magic "Qspt1WmJOL" and its type at 0x0a. Port 50000 packets carry the device
name at 0x0c (20 bytes; the device number at 0x24 in a keep-alive); ports 50001 and 50002 the
name at 0x0b and the device number at 0x21. A beat
packet (type 0x28) carries the pitch at 0x54 (0x00100000 is 0%), BPM x 100 at 0x5a and the beat
within the bar (1 to 4) at 0x5c. Only these are decoded, for the console; the log keeps
everything raw. [Verify the offsets against a real capture.]

Setup: plug the laptop into the switch the CDJs and the mixer share. Without DHCP Windows gives
itself a 169.254.x.y address after about a minute; broadcasts arrive either way. Quit rekordbox
(it holds 50000) and AnyDesk (it holds 50001 for its own discovery) first, including from the
system tray; the logger names whatever holds a port it cannot open. Windows asks once whether
Python may receive on this network (often "Public" here): allow it, or nothing arrives. Stop with
Ctrl+C.
"""

import argparse
import json
import os
import re
import select
import socket
import subprocess
import sys
import time
from collections import defaultdict
from datetime import datetime, timezone

PORTS = (50000, 50001, 50002)
MAGIC = b"Qspt1WmJOL"
TYPE_NAMES = {
    (50000, 0x0A): "hello",
    (50000, 0x00): "claim 1",
    (50000, 0x02): "claim 2",
    (50000, 0x04): "claim 3",
    (50000, 0x06): "keep-alive",
    (50000, 0x08): "conflict",
    (50001, 0x28): "beat",
    (50001, 0x03): "on air",
    (50001, 0x02): "fader start",
    (50001, 0x0B): "position (CDJ-3000)",
    (50001, 0x26): "master handoff request",
    (50001, 0x27): "master handoff response",
    (50001, 0x2A): "sync control",
    (50002, 0x0A): "CDJ status",
    (50002, 0x29): "mixer status",
}


def holder_of(port):
    """The program holding a UDP port, from netstat and tasklist (Windows); None if unknown."""
    try:
        table = subprocess.run(["netstat", "-ano", "-p", "UDP"], capture_output=True, text=True).stdout
        m = re.search(rf"UDP\s+\S+:{port}\s+\S+\s+(\d+)", table)
        if not m:
            return None
        task = subprocess.run(["tasklist", "/FI", f"PID eq {m.group(1)}", "/FO", "CSV", "/NH"],
                              capture_output=True, text=True).stdout
        return task.split(",")[0].strip('"') or f"process {m.group(1)}"
    except OSError:
        return None


def device_name(port, data):
    start = 0x0C if port == 50000 else 0x0B
    return data[start:start + 20].split(b"\0", 1)[0].decode("ascii", "replace").strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", default=os.path.join("data", "djlink"))
    parser.add_argument("--summary-s", type=float, default=2.0)
    parser.add_argument("--host", default="", help="address to listen on (default: all)")
    parser.add_argument("--port-base", type=int, default=50000,
                        help="listen on this and the next two ports (tests only)")
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8", line_buffering=True)

    sockets = {}  # socket -> the Pro DJ Link port it stands for
    for port in PORTS:
        actual = port - 50000 + args.port_base
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        try:
            s.bind((args.host, actual))
        except OSError as e:
            holder = holder_of(actual)
            print(f"port {actual}: cannot listen ({e.strerror})" +
                  (f": {holder} holds it, quit it and start again" if holder else ""))
            s.close()
            continue
        sockets[s] = port
    missing = [p for p in PORTS if p not in sockets.values()]
    if 50001 in missing:
        print("WARNING: no beats or CDJ-3000 positions without port 50001")
    if not sockets:
        sys.exit("no port to listen on")

    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, datetime.now().strftime("djlink-%Y%m%d-%H%M%S.jsonl"))
    print(f"listening on {sorted(p - 50000 + args.port_base for p in sockets.values())}, "
          f"writing {path} (Ctrl+C to stop)")

    counts = defaultdict(int)  # (src, port, type) -> packets
    names = {}  # src -> (name, device number)
    beats = {}  # src -> (bpm, pitch %, beat in bar, t_ns)
    other = 0
    total = 0
    next_summary = time.monotonic() + args.summary_s
    with open(path, "w", encoding="utf-8") as log:
        try:
            while True:
                ready, _, _ = select.select(list(sockets), [], [], 0.5)
                for s in ready:
                    data, (src, _) = s.recvfrom(2048)
                    t_ns = time.perf_counter_ns()
                    port = sockets[s]
                    utc = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
                    log.write(json.dumps({"t_ns": t_ns, "utc": utc, "port": port, "src": src,
                                          "hex": data.hex()}) + "\n")
                    total += 1
                    if not data.startswith(MAGIC) or len(data) < 0x0B:
                        other += 1
                        continue
                    kind = data[0x0A]
                    counts[(src, port, kind)] += 1
                    if len(data) >= 0x22:
                        number = data[0x24] if port == 50000 and len(data) > 0x24 else data[0x21]
                        names.setdefault(src, (device_name(port, data), number))
                    if port == 50001 and kind == 0x28 and len(data) >= 0x5D:
                        pitch = (int.from_bytes(data[0x54:0x58], "big") - 0x100000) / 0x100000 * 100
                        bpm = int.from_bytes(data[0x5A:0x5C], "big") / 100
                        beats[src] = (bpm, pitch, data[0x5C], t_ns)
                now = time.monotonic()
                if now >= next_summary:
                    next_summary = now + args.summary_s
                    log.flush()
                    print(f"--- {datetime.now():%H:%M:%S}: {total} packets ({other} not Pro DJ Link)")
                    for src in sorted({k[0] for k in counts}):
                        name, number = names.get(src, ("?", 0))
                        kinds = ", ".join(
                            f"{TYPE_NAMES.get((p, k), f'{p}/0x{k:02x}')} {n}"
                            for (s, p, k), n in sorted(counts.items()) if s == src)
                        line = f"  {src} {name!r} #{number}: {kinds}"
                        if src in beats:
                            bpm, pitch, beat, t = beats[src]
                            age = (time.perf_counter_ns() - t) / 1e9
                            line += f" | last beat {beat} of the bar, {bpm:.2f} BPM, pitch {pitch:+.2f}%, {age:.1f} s ago"
                        print(line)
        except KeyboardInterrupt:
            pass
    print(f"stopped: {total} packets in {path}")


if __name__ == "__main__":
    main()

"""Print timestamped lines received on a serial port for a fixed time.

Usage:
    tools/.venv/Scripts/python.exe tools/serial_log.py <PORT> <SECONDS> [BAUD]
    tools/.venv/Scripts/python.exe tools/serial_log.py COM7 30 115200 --out run1.txt
    tools/.venv/Scripts/python.exe tools/serial_log.py --list

Each received line is printed as "HH:MM:SS.mmm <line>". With --out FILE the raw
lines (no timestamp) are also written to FILE. Undecodable bytes are shown as
replacement characters.
"""

import argparse
import sys
import time

import serial
from serial.tools import list_ports

DEFAULT_BAUD = 115200


def list_serial_ports():
    ports = sorted(list_ports.comports(), key=lambda p: p.device)
    if not ports:
        print("no serial ports found")
        return 0
    print(f"{'device':<10} {'description':<40} hwid")
    for p in ports:
        print(f"{p.device:<10} {(p.description or '')[:40]:<40} {p.hwid or ''}")
    return 0


def stamp():
    now = time.time()
    return time.strftime("%H:%M:%S", time.localtime(now)) + f".{int(now * 1000) % 1000:03d}"


def run(port, seconds, baud, out_path):
    try:
        ser = serial.Serial(port, baudrate=baud, bytesize=8, parity="N", stopbits=1, timeout=0.1)
    except (serial.SerialException, OSError) as e:
        print(f"error: cannot open {port} at {baud} baud: {e}", file=sys.stderr)
        print("hint: check the port name with --list, and that no other program has it open.",
              file=sys.stderr)
        return 2

    out = open(out_path, "w", encoding="utf-8", errors="replace") if out_path else None
    print(f"reading {port} @ {baud} for {seconds:g} s (Ctrl+C to stop early)", flush=True)
    buf = bytearray()
    lines = 0
    start = time.monotonic()
    try:
        while time.monotonic() - start < seconds:
            chunk = ser.read(ser.in_waiting or 1)
            if not chunk:
                continue
            buf.extend(chunk)
            while True:
                idx = buf.find(b"\n")
                if idx < 0:
                    break
                raw = bytes(buf[:idx])
                del buf[:idx + 1]
                text = raw.decode("utf-8", errors="replace").rstrip("\r")
                lines += 1
                print(f"{stamp()} {text}", flush=True)
                if out:
                    out.write(text + "\n")
        if buf:  # flush a trailing partial line
            text = bytes(buf).decode("utf-8", errors="replace").rstrip("\r\n")
            if text:
                lines += 1
                print(f"{stamp()} {text}", flush=True)
                if out:
                    out.write(text + "\n")
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        if out:
            out.close()
    print(f"done: {lines} line(s) received", flush=True)
    return 0


def main(argv=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    p = argparse.ArgumentParser(description="Timestamped serial line logger")
    p.add_argument("port", nargs="?", help="serial port, e.g. COM7")
    p.add_argument("seconds", nargs="?", type=float, help="how long to read")
    p.add_argument("baud", nargs="?", type=int, default=DEFAULT_BAUD, help="baud rate (default 115200)")
    p.add_argument("--list", action="store_true", help="list serial ports and exit")
    p.add_argument("--out", metavar="FILE", help="also write raw lines to FILE")
    args = p.parse_args(argv)

    if args.list:
        return list_serial_ports()
    if not args.port or args.seconds is None:
        p.error("PORT and SECONDS are required (or use --list)")
    if args.seconds <= 0:
        p.error("SECONDS must be positive")
    return run(args.port, args.seconds, args.baud, args.out)


if __name__ == "__main__":
    sys.exit(main())

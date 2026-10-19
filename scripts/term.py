#!/usr/bin/env python3
"""
Minimal serial terminal for the OV7670 rig — a picocom replacement that also
shows what is being *sent*, and mirrors the whole session to a file.

Everything received is printed with "< ", everything sent with "> ", so the
command/response flow is unambiguous when debugging the protocol.

Usage:
    ./scripts/term.py                       interactive; type commands, Ctrl-C exits
    ./scripts/term.py PING                  send one command, print reply, exit
    ./scripts/term.py "MODE 0" PROBE        send several, in order
    ./scripts/term.py --port /dev/cu.usbmodem1403 --log /tmp/rig.log

Note: only one process can hold the serial port.  Quit this before running
cam.py (and vice versa).
"""

import argparse
import glob
import os
import sys
import threading
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial missing:  pip3 install pyserial")

BAUD = 921600          # fixed; matches UART_BAUD in main.c


def find_port(explicit=None):
    if explicit:
        return explicit
    for pat in ("/dev/cu.usbmodem*", "/dev/tty.usbmodem*"):
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[0]
    sys.exit("no /dev/cu.usbmodem* found — is the Nucleo plugged in?")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("commands", nargs="*", help="commands to send, then exit")
    ap.add_argument("--port")
    ap.add_argument("--log", default=None)
    ap.add_argument("--wait", type=float, default=3.0,
                    help="seconds of silence to wait after the last command")
    ap.add_argument("--raw", action="store_true",
                    help="print received lines without the '< ' prefix")
    args = ap.parse_args()

    port = find_port(args.port)
    log = open(args.log, "w") if args.log else None
    ser = serial.Serial(port, BAUD, timeout=0.2)
    print("=== %s @ %d — Ctrl-C to quit ===" % (port, BAUD), file=sys.stderr)

    stop = threading.Event()
    last_rx = [time.time()]

    def reader():
        buf = b""
        while not stop.is_set():
            try:
                chunk = ser.read(4096)
            except Exception as exc:
                print("!! read failed: %s" % exc, file=sys.stderr)
                stop.set()
                return
            if not chunk:
                continue
            last_rx[0] = time.time()
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", "replace").rstrip("\r")
                print(text if args.raw else "< " + text, flush=True)
                if log:
                    log.write(text + "\n")
                    log.flush()

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    try:
        if args.commands:
            for c in args.commands:
                time.sleep(0.2)
                print("> " + c, flush=True)
                if log:
                    log.write("> " + c + "\n")
                ser.write((c + "\n").encode())
                ser.flush()
                # Wait until the device has been quiet for `wait` seconds.
                last_rx[0] = time.time()
                while time.time() - last_rx[0] < args.wait:
                    time.sleep(0.1)
        else:
            for line in sys.stdin:
                line = line.rstrip("\n")
                print("> " + line, flush=True)
                if log:
                    log.write("> " + line + "\n")
                ser.write((line + "\n").encode())
                ser.flush()
            # stdin closed (e.g. piped input): let replies drain.
            while time.time() - last_rx[0] < args.wait:
                time.sleep(0.1)
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        time.sleep(0.3)
        ser.close()
        if log:
            log.close()


if __name__ == "__main__":
    main()

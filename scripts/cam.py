#!/usr/bin/env python3
"""
Host-side driver for the F401RE + OV7670 strip-capture rig.

The firmware exposes a line-oriented command protocol on USART2 (ST-Link VCP).
Every command is answered by zero or more informational lines followed by
exactly one "OK" or "ERR ...".  That makes the whole rig scriptable, so an
experiment sweep never needs a reflash or a manual picocom session.

Frames larger than the 96 KB SRAM are read as horizontal strips: the firmware
skips a counted number of HREF pulses, captures STRIP_LINES rows, and sends
them base64-encoded with a CRC32.  Because the skip is exact, strips from
different physical frames align perfectly, and freezing AEC/AGC/AWB beforehand
keeps their brightness identical — so the assembled image has no seams.

Typical use:
    ./scripts/cam.py probe                       # measure sensor geometry
    ./scripts/cam.py shot --mode 1               # CIF 352x288 -> also 176x144
    ./scripts/cam.py shot --mode 0 --name vga    # full VGA 640x480
    ./scripts/cam.py sweep                       # probe every mode
    ./scripts/cam.py raw "RD 0C"                 # one-off command
"""

import argparse
import base64
import binascii
import glob
import os
import collections
import queue
import signal
import struct
import sys
import threading
import time
import zlib

try:
    import serial
except ImportError:
    sys.exit("pyserial missing:  pip3 install pyserial")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CAPTURE_DIR = os.path.join(ROOT, "captures")
LOG_PATH = os.path.join(CAPTURE_DIR, "session.log")

# Chroma byte order per mode, determined empirically (see EXPERIMENT-LOG).
#
# UYVY and VYUY differ only in which chroma byte is U and which is V, i.e. the
# line's chroma phase.  The modes that run the sensor's DCW /2 downsampler
# (CIF, QVGA) emit U first; plain VGA, with the scaler bypassed entirely, emits
# V first.  Getting this wrong turns skin tones and pink fabric purple while
# leaving luma — and therefore every geometry check — perfectly correct.
MODE_ORDER = {
    0: "VYUY",   # VGA, no DCW
    1: "UYVY",   # CIF, DCW /2
    2: "UYVY",   # QVGA, DCW /2
    3: "UYVY",   # QCIF built-in
    4: "UYVY",   # QCIF via Linux scaler
    5: "UYVY",   # CIF, VGA window
    6: "UYVY",   # QVGA, VGA window
    7: "VYUY",   # full 656x488 array, no DCW (same phase as VGA)
}
SWAP_CHROMA = {"UYVY": "VYUY", "VYUY": "UYVY",
               "YUYV": "YVYU", "YVYU": "YUYV"}

# Mode table — must mirror ov7670_modes[] in App/Inc/camera_blob.h.
MODES = {
    0: ("VGA", 640, 480),
    1: ("CIF", 352, 288),
    2: ("QVGA", 320, 240),
    3: ("QCIF_BUILTIN", 176, 144),
    4: ("QCIF_LINUX", 176, 144),
    5: ("CIF_FULLWIN", 352, 288),
    6: ("QVGA_FULLWIN", 320, 240),
    7: ("VGA_FULLARRAY", 656, 488),
}
STRIP_LINES = 32          # must match STRIP_LINES in main.c


_T0 = time.time()


def step(msg):
    """Progress line with elapsed time, flushed immediately so a run that
    later stalls still shows exactly how far it got."""
    print("[%6.1fs] %s" % (time.time() - _T0, msg), flush=True)


def arm_watchdog(seconds):
    """Abort the whole run after `seconds`.  Nothing here is worth waiting on
    forever: every firmware command is bounded, so exceeding this budget means
    the link or the board is wedged and the operator needs to know now."""
    def _bark(signum, frame):
        print("!! watchdog: aborting after %ds" % seconds, flush=True)
        os._exit(3)
    signal.signal(signal.SIGALRM, _bark)
    signal.alarm(seconds)


def find_port(explicit=None):
    if explicit:
        return explicit
    for pat in ("/dev/ttyACM*", "/dev/ttyACM0*"):
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[0]
    sys.exit("no /dev/cu.usbmodem* found — is the Nucleo plugged in?")


class Rig:
    """Serial transport plus the command/response state machine.

    Uses a background reader thread feeding a queue — the same arrangement as
    term.py.  An earlier version tried to slice lines out of the stream inside
    the caller's timing loop and dropped replies whenever a read straddled a
    line boundary.
    """

    # One fixed link rate everywhere.  The firmware hardcodes 115200 in
    # MX_USART2_UART_Init(), so this is also what it uses after every reset —
    # there is no negotiated or remembered speed that a crashed host session
    # could leave behind, and `picocom -qrb 115200` always just works.
    BAUD = 115200

    def __init__(self, port, verbose=False):
        self.verbose = verbose
        self.port_name = port
        self.baud = self.BAUD
        os.makedirs(CAPTURE_DIR, exist_ok=True)
        # Truncated per session so stale output can never be mistaken for new.
        self.log = open(LOG_PATH, "w")
        self.ser = serial.Serial(port, self.BAUD, timeout=0.2)
        self.q = queue.Queue()
        # Rolling window of raw lines, dumped when a step fails so the
        # firmware's own DBG/CMD echo explains what went wrong.
        self.recent = collections.deque(maxlen=40)
        self._stop = threading.Event()
        self._rx = threading.Thread(target=self._reader, daemon=True)
        self._rx.start()
        time.sleep(0.3)

    def _reader(self):
        buf = b""
        while not self._stop.is_set():
            try:
                chunk = self.ser.read(4096)
            except Exception as exc:
                print("!! serial read failed: %s" % exc, file=sys.stderr)
                return
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", "replace").rstrip("\r")
                self.log.write(text + "\n")
                if self.verbose:
                    print("  <", text)
                self.recent.append(text)
                self.q.put(text)

    def close(self):
        self._stop.set()
        time.sleep(0.3)
        try:
            self.ser.close()
        finally:
            self.log.close()

    def readline(self, timeout=2.0):
        try:
            return self.q.get(timeout=timeout)
        except queue.Empty:
            return None

    def drain(self, seconds=0.4):
        while self.readline(seconds) is not None:
            pass

    def send(self, text):
        if self.verbose:
            print(">", text)
        self.log.write("> " + text + "\n")
        self.ser.write((text + "\n").encode())
        self.ser.flush()

    def cmd(self, text, timeout=20.0):
        """Send one command, return the lines emitted before its OK."""
        self.drain(0.05)
        self.send(text)
        lines = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.readline(0.5)
            if line is None:
                continue
            if line == "OK":
                return lines
            if line.startswith("ERR"):
                raise RuntimeError("firmware: " + line)
            lines.append(line)
        raise TimeoutError("no OK for %r (got %d lines)" % (text, len(lines)))

    def connect(self, timeout=25.0):
        """Handshake until the link actually carries traffic.

        The ST-Link VCP needs a few seconds after open() before bytes start
        flowing on macOS, and if the board was already running there is no
        banner to wait for.  So poll with PING rather than assuming any fixed
        settling delay — that assumption was silently eating the first reply.
        """
        deadline = time.time() + timeout
        attempt = 0
        while time.time() < deadline:
            attempt += 1
            self.drain(0.05)
            self.send("PING")
            sub = time.time() + 3.0
            while time.time() < sub:
                line = self.readline(0.5)
                if line is None:
                    continue
                if line.startswith("PONG"):
                    print("[%6.1fs]    link up after %d ping(s): %s"
                          % (time.time() - _T0, attempt, line), flush=True)
                    self.drain(0.3)
                    return line
        raise TimeoutError("no PONG in %.0fs — is another program holding %s?"
                           % (timeout, self.port_name))

    def wait_ready(self, timeout=8.0):
        """Wait for the boot banner's READY, then its trailing OK."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.readline(0.5)
            if line is None:
                continue
            if line.startswith("READY"):
                while time.time() < deadline:
                    if self.readline(0.5) == "OK":
                        return True
                return True
        return False

    def hard_reset(self):
        """Reboot via the firmware; fall back to SWD if it is wedged.

        SWD reset is baud- and firmware-state independent, so it is the only
        recovery that always works.
        """
        self.send("RESET")
        if self.wait_ready(5.0):
            return
        print("   soft reset silent — resetting over SWD")
        self.close()
        os.system("st-flash --connect-under-reset reset >/dev/null 2>&1")
        time.sleep(2.0)
        self.__init__(self.port_name, self.verbose)
        self.wait_ready(8.0)

    def frame(self, line_bytes, total_lines, timeout=400.0, verify=False):
        """Fetch a whole frame with a single FRAME command.

        The firmware streams every chunk back-to-back, so there is no
        host->board traffic in the middle of a frame.  Each chunk carries its
        own CRC32 and the frame is followed by a CRC over the concatenation,
        so a torn transfer is detected rather than quietly turned into a
        banded image.
        """
        self.drain(0.05)
        self.send("FRAME %d %d %d" % (line_bytes, total_lines,
                                      1 if verify else 0))

        header = None
        deadline = time.time() + 30.0
        while time.time() < deadline:
            line = self.readline(1.0)
            if line is None:
                continue
            if line.startswith("---FRAME-BEGIN"):
                header = line
                break
            if line.startswith("ERR"):
                raise RuntimeError(line)
            if line.startswith("!!!"):
                raise RuntimeError("board fault: " + line)
        if header is None:
            raise TimeoutError("no FRAME header")
        step("frame header: " + header.strip("-"))

        chunks = []
        cur = None
        b64 = []
        verify_data = None
        frame_crc = None
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.readline(2.0)
            if line is None:
                continue
            if line.startswith("---VERIFY-BEGIN"):
                cur = {"verify": "1"}
                for tok in line.replace("---", " ").replace("VERIFY-BEGIN", " ").split():
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        cur[k] = v
                b64 = []
                continue
            if line.startswith("---VERIFY-END"):
                verify_data = base64.b64decode("".join(b64))
                cur = None
                continue
            if line.startswith("---STRIP-BEGIN"):
                cur = {}
                for tok in line.replace("---", " ").replace("STRIP-BEGIN", " ").split():
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        cur[k] = v
                b64 = []
                continue
            if line.startswith("---STRIP-END"):
                data = base64.b64decode("".join(b64))
                want = int(cur["bytes"])
                wcrc = int(cur["crc"], 16)
                if len(data) != want:
                    raise ValueError("chunk %s: size %d != %d"
                                     % (cur.get("start"), len(data), want))
                if (zlib.crc32(data) & 0xFFFFFFFF) != wcrc:
                    raise ValueError("chunk %s: CRC mismatch" % cur.get("start"))
                chunks.append((int(cur["start"]), data))
                # linemin/linemax must both equal the requested line length;
                # anything else means the read length and the sensor's real
                # line length disagree, which is what shear looks like.
                warn = ""
                lmin, lmax = cur.get("linemin"), cur.get("linemax")
                if lmin != lmax or (lmin and int(lmin) != line_bytes):
                    warn = "  <-- LINE LENGTH MISMATCH (want %d)" % line_bytes
                if cur.get("drop", "0") != "0":
                    warn += "  <-- %s dropped lines" % cur["drop"]
                if cur.get("short", "0") not in ("0", None):
                    warn += "  <-- %s SHORT lines (missed PCLK edge)" % cur["short"]
                step("chunk start=%-4s %6d B  got=%s line=%s..%s%s"
                     % (cur.get("start"), len(data), cur.get("got"),
                        lmin, lmax, warn))
                cur = None
                continue
            if line.startswith("---FRAME-END"):
                for tok in line.replace("---", " ").split():
                    if tok.startswith("crc="):
                        frame_crc = int(tok[4:], 16)
                break
            if line.startswith("ERR"):
                raise RuntimeError(line)
            if line.startswith("!!!"):
                raise RuntimeError("board fault: " + line)
            if (line.startswith("TICK") or line.startswith("CMD ")
                    or line.startswith("DBG")):
                continue
            if cur is not None:
                b64.append(line.strip())

        # Consume the trailing OK so the stream stays in sync.
        t = time.time()
        while time.time() - t < 5.0:
            if self.readline(0.5) == "OK":
                break

        chunks.sort(key=lambda c: c[0])
        frame = bytearray()
        for _, d in chunks:
            frame += d
        if frame_crc is not None:
            got = zlib.crc32(bytes(frame)) & 0xFFFFFFFF
            if got != frame_crc:
                raise ValueError("frame CRC %08X != %08X" % (got, frame_crc))
            step("frame CRC verified %08X" % got)

        if verify_data:
            # Compare the post-frame re-read of strip 0 against the original.
            n = min(len(verify_data), len(frame))
            luma = [(frame[i], verify_data[i]) for i in range(1, n, 2)]
            dif = sum(abs(a - b) for a, b in luma) / len(luma)
            moved = dif > 6.0
            step("motion check: mean luma delta %.2f over strip 0 -> %s"
                 % (dif, "SCENE MOVED (frame is torn)" if moved
                    else "scene static (assembly valid)"))
        return bytes(frame)

    def snap(self, timeout=60.0):
        """Single-frame 2x2-averaged capture (SNAP), returned as raw UYVY.

        One frame, so nothing is composited and a moving subject cannot tear
        the image — the trade against the strip path, which reaches full
        640x480 but spans tens of seconds.
        """
        self.drain(0.05)
        self.send("SNAP")
        hdr = None
        info = None
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.readline(1.0)
            if line is None:
                continue
            if line.startswith("SNAPINFO"):
                info = line
                step(line)
                continue
            if line.startswith("---SNAP-BEGIN"):
                hdr = line
                break
            if line.startswith("ERR"):
                raise RuntimeError(line)
            if line.startswith("!!!"):
                raise RuntimeError("board fault: " + line)
        if hdr is None:
            raise TimeoutError("no SNAP header")

        fields = {}
        for tok in hdr.replace("---", " ").replace("SNAP-BEGIN", " ").split():
            if "=" in tok:
                k, v = tok.split("=", 1)
                fields[k] = v
        b64 = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.readline(2.0)
            if line is None:
                continue
            if line.startswith("---SNAP-END"):
                break
            if (line.startswith("TICK") or line.startswith("CMD ")
                    or line.startswith("DBG")):
                continue
            b64.append(line.strip())
        data = base64.b64decode("".join(b64))
        if len(data) != int(fields["bytes"]):
            raise ValueError("snap size %d != %s" % (len(data), fields["bytes"]))
        if (zlib.crc32(data) & 0xFFFFFFFF) != int(fields["crc"], 16):
            raise ValueError("snap CRC mismatch")
        t = time.time()
        while time.time() - t < 5.0:
            if self.readline(0.5) == "OK":
                break
        step("snap CRC verified %s (%d bytes)" % (fields["crc"], len(data)))
        return data, int(fields["w"]), int(fields["lines"])

    def strip(self, line_bytes, start, lines, retries=3):
        """Fetch one strip, verifying the firmware's CRC32.

        A bad CRC or a short read is retried rather than silently assembled,
        because a single corrupted strip would show up as a torn band in the
        middle of the final image and be easy to mistake for a sensor problem.
        """
        cmd = "STRIP %d %d %d" % (line_bytes, start, lines)
        for attempt in range(retries):
            try:
                self.drain(0.05)
                self.send(cmd)

                hdr = None
                deadline = time.time() + 20.0
                while time.time() < deadline:
                    line = self.readline(0.5)
                    if line is None:
                        continue
                    if line.startswith("---STRIP-BEGIN"):
                        hdr = line
                        break
                    if line.startswith("ERR"):
                        raise RuntimeError(line)
                if hdr is None:
                    raise TimeoutError("no strip header")

                fields = {}
                for tok in hdr.replace("---", " ").replace("STRIP-BEGIN", " ").split():
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        fields[k] = v
                want_bytes = int(fields["bytes"])
                want_crc = int(fields["crc"], 16)

                b64 = []
                deadline = time.time() + 180.0
                while time.time() < deadline:
                    line = self.readline(2.0)
                    if line is None:
                        continue
                    if line.startswith("---STRIP-END"):
                        break
                    # Defensive: never let a diagnostic line become payload.
                    if (line.startswith("TICK") or line.startswith("CMD ")
                            or line.startswith("DBG") or line.startswith("!!!")):
                        continue
                    b64.append(line.strip())
                data = base64.b64decode("".join(b64))

                # Consume the trailing OK so the stream stays in sync.
                deadline = time.time() + 5.0
                while time.time() < deadline:
                    if self.readline(0.5) == "OK":
                        break

                if len(data) != want_bytes:
                    raise ValueError("size %d != %d" % (len(data), want_bytes))
                got_crc = zlib.crc32(data) & 0xFFFFFFFF
                if got_crc != want_crc:
                    raise ValueError("crc %08X != %08X" % (got_crc, want_crc))
                return data
            except Exception as exc:
                print("   strip %d retry %d/%d: %s" % (start, attempt + 1, retries, exc),
                      flush=True)
                print("   --- last lines from the board ---", flush=True)
                for l in list(self.recent)[-15:]:
                    print("     | " + l, flush=True)
                self.drain(0.5)
                # Is the board even alive, or did it fault/hang?
                try:
                    self.send("PING")
                    alive = None
                    t = time.time()
                    while time.time() - t < 3.0:
                        l = self.readline(0.5)
                        if l and l.startswith("PONG"):
                            alive = l
                            break
                    print("   liveness: %s" % (alive or "NO RESPONSE"), flush=True)
                except Exception as e2:
                    print("   liveness check failed: %s" % e2, flush=True)
                self.drain(0.5)
        raise RuntimeError("strip %d failed after %d attempts" % (start, retries))


# =========================================================================
# Image conversion
# =========================================================================

def yuv422_to_rgb(data, w, h, order="UYVY"):
    """UYVY/YUYV 4:2:2 -> RGB888 bytearray, BT.601 studio-swing."""
    out = bytearray(w * h * 3)
    idx = {"UYVY": (1, 0, 3, 2), "YUYV": (0, 1, 2, 3),
           "YVYU": (0, 3, 2, 1), "VYUY": (1, 2, 3, 0)}[order]
    iy0, iu, iy1, iv = idx
    o = 0
    for base in range(0, w * h * 2, 4):
        q = data[base:base + 4]
        if len(q) < 4:
            break
        y0, u, y1, v = q[iy0], q[iu], q[iy1], q[iv]
        u -= 128
        v -= 128
        for y in (y0, y1):
            c = 298 * (y - 16 if y >= 16 else 0)
            r = (c + 409 * v + 128) >> 8
            g = (c - 100 * u - 208 * v + 128) >> 8
            b = (c + 516 * u + 128) >> 8
            out[o] = 0 if r < 0 else (255 if r > 255 else r)
            out[o + 1] = 0 if g < 0 else (255 if g > 255 else g)
            out[o + 2] = 0 if b < 0 else (255 if b > 255 else b)
            o += 3
    return out


def box_downscale(rgb, w, h, fx, fy):
    """Exact box average by integer factors — no ringing, no aliasing."""
    ow, oh = w // fx, h // fy
    out = bytearray(ow * oh * 3)
    n = fx * fy
    for oy in range(oh):
        for ox in range(ow):
            acc = [0, 0, 0]
            for dy in range(fy):
                row = (oy * fy + dy) * w
                for dx in range(fx):
                    p = (row + ox * fx + dx) * 3
                    acc[0] += rgb[p]
                    acc[1] += rgb[p + 1]
                    acc[2] += rgb[p + 2]
            q = (oy * ow + ox) * 3
            out[q] = acc[0] // n
            out[q + 1] = acc[1] // n
            out[q + 2] = acc[2] // n
    return out, ow, oh


def area_resize(rgb, w, h, ow, oh):
    """General area-average resize for non-integer ratios (e.g. 640->176)."""
    out = bytearray(ow * oh * 3)
    xb = [(x * w // ow, max((x + 1) * w // ow, x * w // ow + 1)) for x in range(ow)]
    yb = [(y * h // oh, max((y + 1) * h // oh, y * h // oh + 1)) for y in range(oh)]
    for oy in range(oh):
        y0, y1 = yb[oy]
        for ox in range(ow):
            x0, x1 = xb[ox]
            acc = [0, 0, 0]
            n = 0
            for yy in range(y0, y1):
                row = yy * w
                for xx in range(x0, x1):
                    p = (row + xx) * 3
                    acc[0] += rgb[p]
                    acc[1] += rgb[p + 1]
                    acc[2] += rgb[p + 2]
                    n += 1
            q = (oy * ow + ox) * 3
            out[q] = acc[0] // n
            out[q + 1] = acc[1] // n
            out[q + 2] = acc[2] // n
    return out


def write_png(path, rgb, w, h):
    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", binascii.crc32(tag + payload) & 0xFFFFFFFF))
    raw = bytearray()
    for y in range(h):
        raw += b"\x00" + rgb[y * w * 3:(y + 1) * w * 3]
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)
    return path


# =========================================================================
# Commands
# =========================================================================

# Window defaults, mirroring ov7670_modes[] in camera_blob.h.
OV_WINDOWS = {
    0: (158, 14, 10, 490),
    1: (170, 90, 14, 494),
    2: (168, 24, 12, 492),
    3: (158, 14, 10, 490),
    4: (456, 24, 14, 494),
    5: (158, 14, 10, 490),
    6: (158, 14, 10, 490),
    7: (158, 30, 10, 498),
}


def ov_window_defaults(mode):
    return OV_WINDOWS.get(mode, (158, 14, 10, 490))


def do_probe(rig, mode=None):
    if mode is not None:
        for l in rig.cmd("MODE %d" % mode, timeout=15):
            print("  ", l)
    out = rig.cmd("PROBE", timeout=15)
    for l in out:
        print("  ", l)
    geom = {}
    for l in out:
        if l.startswith("PROBE href_lines"):
            for tok in l.split()[1:]:
                k, v = tok.split("=")
                geom[k] = int(v)
    return geom


def chroma_roughness(frame, width, height):
    """Mean |difference| between horizontally adjacent U samples.

    A real scene varies smoothly, so this stays low; per-pixel sampling errors
    (a byte latched at the wrong instant) make it jump.  Measured values:
    ~1.9 for a good VGA capture, ~8.7 when the window is opened past 640 and
    the sensor's output degrades.  Having a number makes "looks fine to me"
    unnecessary.
    """
    lb = width * 2
    tot = n = 0
    for y in range(0, height, 4):
        u = frame[y * lb:(y + 1) * lb][0::4]
        for i in range(1, len(u)):
            tot += abs(u[i] - u[i - 1])
            n += 1
    return tot / n if n else 0.0


def crop_left(frame, width, height, ncols):
    """Drop `ncols` leading pixels from every line.

    Must be an even number of pixels: one UYVY group covers two pixels
    (U Y0 V Y1), so cropping an odd count would start a line on V instead of U
    and swap the chroma channels — which is exactly what shifting the sensor's
    hardware window by 4 input pixels did, turning skin tones blue.
    """
    assert ncols % 2 == 0, "crop must be an even pixel count"
    lb = width * 2
    keep = width - ncols
    out = bytearray()
    for y in range(height):
        row = frame[y * lb:(y + 1) * lb]
        out += row[ncols * 2:]
    return bytes(out), keep


def capture_frame(rig, mode, width, height, tag, orders=("UYVY",),
                  downscales=((176, 144),), autocrop=False, verify=True):
    """Capture `height` lines of `width` pixels as strips, assemble, save."""
    line_bytes = width * 2
    t0 = time.time()
    frame = rig.frame(line_bytes, height, verify=verify)
    dt = time.time() - t0
    lines_got = len(frame) // (width * 2)
    rough = chroma_roughness(frame, width, lines_got)
    verdict = "clean" if rough < 3.0 else ("marginal" if rough < 5.0 else "NOISY")
    step("frame complete: %d bytes in %.1fs  chroma_roughness=%.2f (%s)"
         % (len(frame), dt, rough, verdict))

    if autocrop:
        # The sensor emits a few dummy columns before real pixel data.  Detect
        # them from the image itself and drop them, rounded up to an even
        # pixel count to preserve UYVY phase.
        lines = len(frame) // (width * 2)
        spread = col_spreads(frame, width, lines)
        dead = lead_dead(spread)
        if dead:
            ncols = dead + (dead % 2)
            frame, width = crop_left(frame, width, lines, ncols)
            height = lines
            step("cropped %d dummy columns -> %dx%d" % (ncols, width, height))

    os.makedirs(CAPTURE_DIR, exist_ok=True)
    stamp = time.strftime("%H%M%S")
    base = "%s_m%d_%s_%dx%d" % (stamp, mode, tag, width, height)

    raw_path = os.path.join(CAPTURE_DIR, base + ".yuv")
    with open(raw_path, "wb") as f:
        f.write(frame)

    results = []
    for order in orders:
        rgb = yuv422_to_rgb(frame, width, height, order)
        suffix = "" if order == "UYVY" and len(orders) == 1 else "_" + order
        p = write_png(os.path.join(CAPTURE_DIR, base + suffix + ".png"),
                      rgb, width, height)
        results.append(p)
        step("wrote " + os.path.relpath(p, ROOT))

        if order != orders[0]:
            continue
        for (ow, oh) in downscales:
            if ow >= width and oh >= height:
                continue
            if width % ow == 0 and height % oh == 0:
                small, sw, sh = box_downscale(rgb, width, height,
                                             width // ow, height // oh)
                how = "box%dx%d" % (width // ow, height // oh)
            else:
                small = area_resize(rgb, width, height, ow, oh)
                sw, sh = ow, oh
                how = "area"
            p = write_png(os.path.join(
                CAPTURE_DIR, "%s_to_%dx%d_%s.png" % (base, sw, sh, how)),
                small, sw, sh)
            results.append(p)
            step("wrote %s (%s)" % (os.path.relpath(p, ROOT), how))
    return results


def col_spreads(frame, width, lines, n=32, right=False):
    """Per-column luma spread down the strip, from the left or right edge."""
    lb = width * 2
    out = []
    xs = range(width - 1, width - 1 - n, -1) if right else range(min(width, n))
    for x in xs:
        ys = [frame[y * lb + 2 * x + 1] for y in range(lines)]
        out.append(max(ys) - min(ys))
    return out


def row_spreads(frame, width, lines, n=8):
    """Per-row luma spread across the line — catches dead rows at the top."""
    lb = width * 2
    out = []
    for y in range(min(lines, n)):
        ys = [frame[y * lb + 2 * x + 1] for x in range(width)]
        out.append(max(ys) - min(ys))
    return out


def lead_dead(spread, thresh=3):
    d = 0
    for sp in spread:
        if sp <= thresh:
            d += 1
        else:
            break
    return d


def count_dead_columns(frame, width, lines, thresh=3):
    """How many leading columns carry no picture information.

    A dummy column emitted by the sensor's output stage is essentially
    constant down the whole strip, whereas real image data varies.  Comparing
    per-column luma spread is a far more reliable test than looking at the PNG.
    """
    lb = width * 2
    spread = []
    for x in range(min(width, 32)):
        ys = [frame[y * lb + 2 * x + 1] for y in range(lines)]
        spread.append(max(ys) - min(ys))
    dead = 0
    for sp in spread:
        if sp <= thresh:
            dead += 1
        else:
            break
    return dead, spread


def do_edge(rig, args):
    """Sweep hardware-window candidates and report the dead-column count.

    One FRAME command per candidate (a single 32-line chunk, ~3 s), so the
    whole sweep is fast and each measurement is a number rather than an
    impression.
    """
    lines = 32
    candidates = []
    for mode in (1, 5):
        m = MODES[mode]
        candidates.append((mode, None, None))
    # Shift CIF's window right in input pixels; DCW /2 means 2 input px per
    # output column, so 18 input px should retire 9 dummy columns.
    for hs in (174, 178, 182, 186, 190, 198, 206):
        candidates.append((1, hs, 90))

    results = []
    for (mode, hs, he) in candidates:
        try:
            rig.cmd("MODE %d" % mode, timeout=25)
            label = "mode=%d %s" % (mode, MODES[mode][0])
            if hs is not None:
                rig.cmd("WIN %d %d 14 494" % (hs, he), timeout=15)
                label += " hstart=%d" % hs
            rig.cmd("WARM 4", timeout=15)
            rig.cmd("FREEZE", timeout=15)
            width = MODES[mode][1]
            frame = rig.frame(width * 2, lines, timeout=60)
            got = len(frame) // (width * 2)
            left = col_spreads(frame, width, got)
            right = col_spreads(frame, width, got, right=True)
            rows = row_spreads(frame, width, got)
            dl, dr = lead_dead(left), lead_dead(right)
            dead = dl + dr
            step("%-28s deadL=%d deadR=%d  L=%s R=%s rows=%s" %
                 (label, dl, dr, left[:10], right[:6], rows[:4]))
            # Save each candidate so the choice can be eyeballed too.
            rgb = yuv422_to_rgb(frame, width, got, "UYVY")
            tag = label.replace(" ", "_").replace("=", "")
            write_png(os.path.join(CAPTURE_DIR, "edge_%s.png" % tag),
                      rgb, width, got)
            results.append((dead, label))
        except Exception as exc:
            step("%-28s FAILED: %s" % (label, exc))
    if results:
        results.sort(key=lambda r: r[0])
        step("best: %s (%d dead columns total)" % (results[0][1], results[0][0]))
        step("candidate PNGs in captures/edge_*.png")


def do_snap(rig, args):
    """Single-frame 176x144: CIF 352x288 averaged 2x2 inside the MCU."""
    for l in rig.cmd("MODE 1", timeout=25):
        print("  ", l)
    if not args.no_freeze:
        for l in rig.cmd("WARM 8", timeout=20):
            print("  ", l)
        for l in rig.cmd("FREEZE", timeout=15):
            print("  ", l)
    t0 = time.time()
    data, w, h = rig.snap()
    step("snap complete in %.1fs" % (time.time() - t0))
    rgb = yuv422_to_rgb(data, w, h, MODE_ORDER[1])
    stamp = time.strftime("%H%M%S")
    p = write_png(os.path.join(CAPTURE_DIR,
                  "%s_snap_%dx%d.png" % (stamp, w, h)), rgb, w, h)
    step("wrote " + os.path.relpath(p, ROOT))
    with open(os.path.join(CAPTURE_DIR, "%s_snap_%dx%d.yuv" % (stamp, w, h)),
              "wb") as f:
        f.write(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("action",
                    choices=["probe", "shot", "snap", "sweep", "edge", "raw",
                             "monitor", "reset"])
    ap.add_argument("rest", nargs="*")
    ap.add_argument("--port")
    ap.add_argument("--mode", type=int, default=1)
    ap.add_argument("--name", default="shot")
    ap.add_argument("--width", type=int)
    ap.add_argument("--height", type=int)
    ap.add_argument("--orders", default=None,
                    help="chroma order(s); defaults to MODE_ORDER for the mode")
    ap.add_argument("--no-verify", action="store_true",
                    help="skip the post-frame motion check")
    ap.add_argument("--alt-chroma", action="store_true",
                    help="also write the chroma-swapped render as a control")
    ap.add_argument("--clkrc", help="override CLKRC (hex), e.g. BF = /64 "
                                    "to halve PCLK and widen timing margin")
    ap.add_argument("--autocrop", action="store_true",
                    help="drop the sensor's leading dummy columns")
    ap.add_argument("--hstart", type=int, help="override window hstart")
    ap.add_argument("--hstop", type=int, help="override window hstop")
    ap.add_argument("--vstart", type=int, help="override window vstart")
    ap.add_argument("--vstop", type=int, help="override window vstop")
    ap.add_argument("--no-freeze", action="store_true")
    ap.add_argument("--fresh", action="store_true",
                    help="reset the MCU over SWD before opening the port")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--timeout", type=int, default=300,
                    help="hard watchdog for the whole run, seconds")
    args = ap.parse_args()

    arm_watchdog(args.timeout)
    port = find_port(args.port)
    if args.fresh:
        step("SWD reset (st-flash) before opening %s" % port)
        os.system("st-flash --connect-under-reset reset >/dev/null 2>&1")
        time.sleep(2.5)
    step("opening %s" % port)
    rig = Rig(port, verbose=args.verbose)
    try:
        rig.connect()

        if args.action == "reset":
            rig.hard_reset()
            return

        if args.action == "monitor":
            while True:
                l = rig.readline(1.0)
                if l is not None:
                    print(l)

        if args.action == "raw":
            for l in rig.cmd(" ".join(args.rest), timeout=30):
                print("  ", l)
            return

        if args.action == "sweep":
            for m in sorted(MODES):
                name, w, h = MODES[m]
                print("\n=== mode %d %s (nominal %dx%d) ===" % (m, name, w, h))
                try:
                    g = do_probe(rig, m)
                    if g:
                        print("   -> real %dx%d px" %
                              (g.get("bytes_first", 0) // 2, g.get("href_lines", 0)))
                except Exception as exc:
                    print("   FAILED:", exc)
            return

        if args.action == "snap":
            do_snap(rig, args)
            return

        if args.action == "edge":
            do_edge(rig, args)
            return

        if args.action == "probe":
            do_probe(rig, args.mode)
            return

        # --- shot ---------------------------------------------------------
        name, w, h = MODES[args.mode]
        for l in rig.cmd("MODE %d" % args.mode, timeout=20):
            print("  ", l)
        if args.clkrc:
            for l in rig.cmd("WR 11 %s" % args.clkrc, timeout=15):
                print("  ", l)
        if args.hstart is not None:
            m = ov_window_defaults(args.mode)
            for l in rig.cmd("WIN %d %d %d %d" % (
                    args.hstart,
                    args.hstop if args.hstop is not None else m[1],
                    args.vstart if args.vstart is not None else m[2],
                    args.vstop if args.vstop is not None else m[3]), timeout=15):
                print("  ", l)
        g = do_probe(rig, None)
        real_w = g.get("bytes_first", 0) // 2
        real_h = g.get("href_lines", 0)
        width = args.width or (real_w if real_w else w)
        height = args.height or (real_h if real_h else h)
        if (width, height) != (w, h):
            print("   note: sensor reports %dx%d, nominal is %dx%d — using %dx%d"
                  % (real_w, real_h, w, h, width, height))

        if not args.no_freeze:
            for l in rig.cmd("WARM 8", timeout=15):
                print("  ", l)
            for l in rig.cmd("FREEZE", timeout=10):
                print("  ", l)

        downs = [(176, 144)]
        if args.orders:
            orders = tuple(args.orders.split(","))
        else:
            # Just the correct order for this mode.  The chroma-swapped render
            # is available via --alt-chroma; it exists only as a control for
            # confirming the order against the scene, and looks wrong by
            # design, which is confusing when left on.
            primary = MODE_ORDER.get(args.mode, "UYVY")
            orders = (primary, SWAP_CHROMA[primary]) if args.alt_chroma \
                else (primary,)
        capture_frame(rig, args.mode, width, height, args.name,
                      orders=orders, downscales=downs,
                      autocrop=args.autocrop, verify=not args.no_verify)
    finally:
        rig.close()


if __name__ == "__main__":
    main()

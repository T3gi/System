import re
import statistics
import sys

def analyze(log_file):
    events = []
    pattern = re.compile(r"EVT t_us=(\d+) name=(\S+)")
    
    with open(log_file, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = pattern.search(line)
            if m:
                events.append((int(m.group(1)), m.group(2)))

    def latencies(start, end):
        out, t0 = [], None
        for t, name in events:
            if name == start:
                t0 = t
            elif name == end and t0 is not None:
                out.append(t - t0)
                t0 = None
        return out

    pairs = [("btn_press", "capture_start"), ("capture_start", "capture_done")]
    for a, b in pairs:
        d = latencies(a, b)
        if d:
            print(f"{a} -> {b}: n={len(d)} min={min(d)}us "
                  f"median={statistics.median(d):.1f}us max={max(d)}us")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        analyze(sys.argv[1])
    else:
        print("Usage: python log_analyzer.py <run.log>")
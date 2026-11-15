"""Зведені метрики ЛР3 з UART-логу прошивки.

Використання:
    python3 scripts/lab3_stats.py docs/metrics/run_btn.log та інші

Рахує:
  * період і джитер TICK (мін / медіана / p99 / макс, пропущені запуски);
  * затримку реакції на кнопку за полем latency_us (DWT), а не за різницею t_us:
    при 84 МГц лічильник DWT переповнюється кожні ~51 с, тож t_us «перескакує»;
  * LOAD, читання температури, події DAMPER, лічильники втрат UART (ore/drop);
  * орієнтовний час старту: t першого TICK мінус 1000 мс.
"""
import re
import statistics
import sys

TICK_PERIOD_MS = 1000


def pct(values, p):
    if not values:
        return None
    s = sorted(values)
    k = min(len(s) - 1, max(0, round(p / 100 * (len(s) - 1))))
    return s[k]


def summary(name, values, unit):
    if not values:
        print(f"  {name}: немає даних")
        return
    print(f"  {name}: n={len(values)} min={min(values)} avg={statistics.mean(values):.1f} "
          f"median={statistics.median(values)} p99={pct(values, 99)} max={max(values)} {unit}")


def analyze(path):
    ticks, lat_us, load = [], [], []
    temp_ok = temp_err = 0
    damper, last_ore, last_drop, header = [], None, None, None
    segments, boots, journal = [], [], []

    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if header is None and line.startswith("#"):
                header = line
            m = re.match(r"TICK t=(\d+) rx=\d+ ore=(\d+) drop=(\d+)", line)
            if m:
                t = int(m.group(1))
                if ticks and t < ticks[-1]:         # плата перезапустилась: новий відрізок
                    boots.append(t)
                    segments.append(ticks)
                    ticks = []
                if not ticks or t > ticks[-1]:      # повтори рядків у лозі пропускаємо
                    ticks.append(t)
                last_ore, last_drop = int(m.group(2)), int(m.group(3))
                continue
            m = re.search(r"latency_us=(\d+)", line)
            if m:
                lat_us.append(int(m.group(1)))
                continue
            m = re.match(r"LOAD pct=(\d+)", line)
            if m:
                load.append(int(m.group(1)))
                continue
            if "Температура:" in line:
                temp_ok += 1
            elif "Помилка отримання температури" in line:
                temp_err += 1
            elif line.startswith("DAMPER "):
                damper.append(line)
            elif line.startswith(("JOURNAL", "LOG ")):
                journal.append(line)

    segments.append(ticks)
    print(f"== {path}")
    if header:
        print(f"  {header}")
    dt = [b - a for seg in segments for a, b in zip(seg, seg[1:])]
    n_ticks = sum(len(seg) for seg in segments)
    print(f"  TICK: {n_ticks} рядків, відрізків між перезапусками: {len(segments)}")
    for t in boots:
        print(f"  перезапуск: перший TICK t={t} мс -> READY ≈ {t - TICK_PERIOD_MS} мс після Reset")
    summary("період TICK", dt, "мс")
    if dt:
        jit = [d - TICK_PERIOD_MS for d in dt]
        missed = sum(1 for d in dt if d > 2 * TICK_PERIOD_MS)
        print(f"  джитер TICK: max=+{max(jit)} мс, пропущених запусків (>2 періодів): {missed}")
    summary("затримка кнопки (DWT), усі", [round(v / 1000, 1) for v in lat_us], "мс")
    summary("  з них < 1 с (звичайна робота)", [round(v / 1000, 3) for v in lat_us if v < 1_000_000], "мс")
    summary("  з них >= 1 с (під час передачі)", [round(v / 1000) for v in lat_us if v >= 1_000_000], "мс")
    summary("LOAD", load, "%")
    print(f"  температура: OK={temp_ok} помилок={temp_err}")
    print(f"  UART RX: ore={last_ore} drop={last_drop} (останній TICK)")
    for d in damper:
        print(f"  {d}")
    for j in journal:
        print(f"  {j}")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    for p in sys.argv[1:]:
        analyze(p)

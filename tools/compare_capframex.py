#!/usr/bin/env python3
"""
Side-by-side comparison of external CapFrameX CSV captures (Windows x64).

This is the EXTERNAL half of OptiGD's benchmark evidence, and it is deliberately
kept separate from the in-game Benchmark Mode:

  * OptiGD's own sessions measure inter-frame intervals inside the game thread.
    They can see per-subsystem CPU cost, culling/particle counters and the
    internal passthrough baseline. They cannot see presented frames.
  * CapFrameX measures the frames the GPU actually presented. It can see
    frametime distributions and stutter from any cause. It cannot see OptiGD's
    internals.

Only the second one can support a claim about how the game runs on a machine, so
this script never merges the two: it takes CapFrameX CSVs only, and every table
it prints says where the numbers came from.

Percentiles use the same definition as OptiGD's own statistics (P50 = median,
P95/P99 = nearest-rank, `rank = ceil(p * n)`, `index = rank - 1`) so an external
table and an internal session record can be read next to each other without a
silent change of meaning.

Nothing here tests for statistical significance. Alternating captures of a single
level cannot separate an optimization effect from run-to-run variability, and the
output says so.

Usage:
    python3 tools/compare_capframex.py --a vanilla_run1.csv vanilla_run2.csv \
                                       --b optigd_run1.csv optigd_run2.csv
    python3 tools/compare_capframex.py --a ... --b ... --json out.json
    python3 tools/compare_capframex.py --self-test

CSV reading is defensive on purpose: CapFrameX exports have changed column layout
and delimiter between versions, and no CapFrameX CSV could be obtained in the
environment where this tool was written (network egress is restricted). The
delimiter is detected (`,` `;` or tab) and the frametime column is matched by
name (`frametime`, `frame time`, `frame_times`, ...), falling back to an FPS
column if that is all there is. `--delimiter`, `--frametime-column` and
`--fps-column` override the detection. If a file cannot be interpreted the script
fails with a message naming the file and the columns it found - it never guesses
numbers into existence.

Exit codes: 0 = compared, 1 = a capture could not be read or is degenerate,
2 = usage error.
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import math
import sys
import tempfile
from pathlib import Path

SOURCE = "CapFrameX (external presented-frame capture)"

FRAMETIME_HEADER_HINTS = ("frametime", "frame time", "frame_times", "frame-times",
                          "frametimesinmilliseconds", "frametime(ms)", "ft(ms)")
FPS_HEADER_HINTS = ("fps", "framespersecond", "frames per second")
TIME_HEADER_HINTS = ("timeinseconds", "time(s)", "time [s]", "timestamp", "time")


def percentile_nearest_rank(sorted_values, p):
    """The definition OptiGD uses everywhere: rank = ceil(p*n), index = rank-1."""
    n = len(sorted_values)
    if n == 0:
        return 0.0
    if p <= 0.0:
        return sorted_values[0]
    if p >= 1.0:
        return sorted_values[-1]
    rank = math.ceil(p * n)
    rank = max(1, min(n, rank))
    return sorted_values[rank - 1]


def median(sorted_values):
    n = len(sorted_values)
    if n == 0:
        return 0.0
    if n % 2 == 0:
        return 0.5 * (sorted_values[n // 2 - 1] + sorted_values[n // 2])
    return sorted_values[n // 2]


def one_percent_low_fps(frame_times_ms):
    """Average FPS of the worst 1% of frames (CapFrameX's '1% Low' convention)."""
    n = len(frame_times_ms)
    if n == 0:
        return 0.0
    worst = max(1, math.ceil(n * 0.01))
    tail = sorted(frame_times_ms)[-worst:]
    mean_worst = sum(tail) / len(tail)
    return (1000.0 / mean_worst) if mean_worst > 0 else 0.0


def detect_delimiter(sample: str) -> str:
    try:
        return csv.Sniffer().sniff(sample, delimiters=",;\t").delimiter
    except csv.Error:
        # A single-column header is legitimate (one frametime column, no metadata);
        # fall back to a comma rather than failing on a file we can still read.
        return ","


def pick_column(fieldnames, hints, explicit=None):
    if explicit:
        for name in fieldnames:
            if name.strip().lower() == explicit.strip().lower():
                return name
        return None
    for hint in hints:
        for name in fieldnames:
            cleaned = name.strip().lower().replace("_", " ")
            if hint.replace("_", " ") in cleaned:
                return name
    return None


def read_capture(path: Path, delimiter="auto", frametime_column=None, fps_column=None):
    """Returns a dict describing one capture, or raises ValueError with a reason."""
    text = path.read_text(encoding="utf-8-sig", errors="replace")
    if not text.strip():
        raise ValueError("%s is empty" % path)

    sample = text[:8192]
    delim = detect_delimiter(sample) if delimiter == "auto" else delimiter
    reader = csv.DictReader(io.StringIO(text), delimiter=delim)
    fieldnames = reader.fieldnames or []
    if not fieldnames:
        raise ValueError("%s has no header row" % path)

    ft_name = pick_column(fieldnames, FRAMETIME_HEADER_HINTS, frametime_column)
    fps_name = pick_column(fieldnames, FPS_HEADER_HINTS, fps_column)
    time_name = pick_column(fieldnames, TIME_HEADER_HINTS)
    if not ft_name and not fps_name:
        raise ValueError("%s has no frametime or FPS column (found: %s)"
                         % (path, ", ".join(fieldnames[:12])))

    frame_times = []
    rejected = 0
    rows = 0
    for row in reader:
        rows += 1
        raw = (row.get(ft_name) if ft_name else row.get(fps_name)) or ""
        raw = raw.strip().replace(",", ".")
        if not raw:
            rejected += 1
            continue
        try:
            value = float(raw)
        except ValueError:
            rejected += 1
            continue
        if not math.isfinite(value) or value <= 0.0:
            # A zero or negative frametime is not a frame. Counted, never dropped
            # silently and never averaged in.
            rejected += 1
            continue
        frame_times.append(value if ft_name else 1000.0 / value)

    if len(frame_times) < 2:
        raise ValueError("%s yielded %d usable frames (%d rows rejected): not a capture"
                         % (path, len(frame_times), rejected))

    duration_s = 0.0
    if time_name:
        times = []
        for row in csv.DictReader(io.StringIO(text), delimiter=delim):
            raw = (row.get(time_name) or "").strip().replace(",", ".")
            try:
                times.append(float(raw))
            except ValueError:
                continue
        if len(times) >= 2:
            duration_s = max(times) - min(times)
    if duration_s <= 0.0:
        duration_s = sum(frame_times) / 1000.0

    ordered = sorted(frame_times)
    n = len(ordered)
    mean_ft = sum(ordered) / n
    stats = {
        "file": path.name,
        "source": SOURCE,
        "frametime_column": ft_name or ("derived from %s" % fps_name),
        "delimiter": repr(delim),
        "frames": n,
        "rows_rejected": rejected,
        "duration_s": round(duration_s, 3),
        "mean_frametime_ms": round(mean_ft, 4),
        "median_frametime_ms": round(median(ordered), 4),
        "p95_frametime_ms": round(percentile_nearest_rank(ordered, 0.95), 4),
        "p99_frametime_ms": round(percentile_nearest_rank(ordered, 0.99), 4),
        "min_frametime_ms": round(ordered[0], 4),
        "max_frametime_ms": round(ordered[-1], 4),
        "mean_fps": round(1000.0 / mean_ft, 3) if mean_ft > 0 else 0.0,
        "fps_from_duration": round(n / duration_s, 3) if duration_s > 0 else 0.0,
        "one_pct_low_fps": round(one_percent_low_fps(frame_times), 3),
    }
    return stats


def pool(captures):
    """Pooled statistics over several captures of the same side."""
    frames = sum(c["frames"] for c in captures)
    duration = sum(c["duration_s"] for c in captures)
    if frames == 0:
        return None
    # Pooling averages the per-capture statistics weighted by frame count. The raw
    # frame times of different runs are NOT concatenated: that would hide which
    # run a tail frame came from.
    def weighted(key):
        return sum(c[key] * c["frames"] for c in captures) / frames

    spread = {
        "mean_frametime_ms": [c["mean_frametime_ms"] for c in captures],
        "p99_frametime_ms": [c["p99_frametime_ms"] for c in captures],
        "one_pct_low_fps": [c["one_pct_low_fps"] for c in captures],
    }
    return {
        "source": SOURCE,
        "captures": len(captures),
        "frames": frames,
        "duration_s": round(duration, 3),
        "mean_frametime_ms": round(weighted("mean_frametime_ms"), 4),
        "median_frametime_ms": round(weighted("median_frametime_ms"), 4),
        "p95_frametime_ms": round(weighted("p95_frametime_ms"), 4),
        "p99_frametime_ms": round(weighted("p99_frametime_ms"), 4),
        "mean_fps": round(weighted("mean_fps"), 3),
        "one_pct_low_fps": round(weighted("one_pct_low_fps"), 3),
        "run_to_run_spread": {
            key: {"min": round(min(v), 4), "max": round(max(v), 4),
                  "range": round(max(v) - min(v), 4)}
            for key, v in spread.items()
        },
    }


METRICS = [
    ("mean_frametime_ms", "Mean frametime", "ms", "lower"),
    ("median_frametime_ms", "Median frametime (P50)", "ms", "lower"),
    ("p95_frametime_ms", "P95 frametime", "ms", "lower"),
    ("p99_frametime_ms", "P99 frametime", "ms", "lower"),
    ("mean_fps", "Mean FPS", "fps", "higher"),
    ("one_pct_low_fps", "1% low FPS", "fps", "higher"),
]


def fmt(value):
    return ("%.3f" % value).rstrip("0").rstrip(".") if isinstance(value, float) else str(value)


def relative(a, b):
    if a == 0:
        return None
    return (b - a) / a * 100.0


def render(label_a, pooled_a, label_b, pooled_b):
    lines = []
    lines.append("== External frame-time comparison ==")
    lines.append("Source of every number below: %s" % SOURCE)
    lines.append("OptiGD internal telemetry is NOT included in this table.")
    lines.append("")
    lines.append("A: %s (%d capture%s, %d frames, %.1f s)"
                 % (label_a, pooled_a["captures"], "" if pooled_a["captures"] == 1 else "s",
                    pooled_a["frames"], pooled_a["duration_s"]))
    lines.append("B: %s (%d capture%s, %d frames, %.1f s)"
                 % (label_b, pooled_b["captures"], "" if pooled_b["captures"] == 1 else "s",
                    pooled_b["frames"], pooled_b["duration_s"]))
    lines.append("Percentiles: P50 = median, P95/P99 = nearest-rank (same definition OptiGD uses).")
    lines.append("")
    header = "%-24s %18s %18s %16s" % ("metric", label_a[:18], label_b[:18], "B vs A")
    lines.append(header)
    lines.append("-" * len(header))
    for key, name, unit, better in METRICS:
        a = pooled_a[key]
        b = pooled_b[key]
        pct = relative(a, b)
        if pct is None:
            delta = "n/a (A is 0)"
        else:
            direction = ""
            if abs(pct) >= 0.05:
                improved = (pct < 0) if better == "lower" else (pct > 0)
                direction = " better" if improved else " worse"
            delta = "%+.2f%%%s" % (pct, direction)
        lines.append("%-24s %18s %18s %16s"
                     % (name, "%s %s" % (fmt(a), unit), "%s %s" % (fmt(b), unit), delta))
    lines.append("")
    lines.append("Run-to-run spread (the reason not to read one pair as a result):")
    for label, pooled in ((label_a, pooled_a), (label_b, pooled_b)):
        for key, span in pooled["run_to_run_spread"].items():
            lines.append("  %-14s %-20s min %s / max %s / range %s"
                         % (label[:14], key, fmt(span["min"]), fmt(span["max"]),
                            fmt(span["range"])))
    lines.append("")
    lines.append("No statistical significance is claimed or computed. This is a")
    lines.append("descriptive comparison of a small number of captures: it cannot")
    lines.append("separate an optimization effect from run-to-run variability.")
    lines.append("Capture at least 3 runs per side, alternating sides, with the")
    lines.append("same level, segment, resolution, refresh rate, FPS limiter and")
    lines.append("background load, before drawing any conclusion.")
    lines.append("")
    lines.append("What this table cannot see: OptiGD's per-subsystem CPU cost, its")
    lines.append("culling/particle counters, or its internal passthrough baseline.")
    lines.append("Those exist only in OptiGD's own session records - report both,")
    lines.append("labelled, and never merge them into one table.")
    return "\n".join(lines)


def self_test():
    """Exercise the reader and the statistics on synthetic CSVs.

    The numbers below are constructed fixtures, not captures: no CapFrameX CSV was
    available in the environment where this tool was written. They exist to prove
    the reader handles the delimiter/header variations CapFrameX has produced and
    that the percentile definition matches OptiGD's.
    """
    errors = []
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)

        # Comma delimiter, CapFrameX-style headers, one blank row and one
        # non-numeric row that must be rejected rather than averaged in.
        a = root / "vanilla_run1.csv"
        a.write_text(
            "TimeInSeconds,FrameTimesInMilliseconds,CPU Usage,GPU Usage\n"
            + "".join("%d,%s,12.5,88.0\n" % (i // 60, 20.0 if i % 50 else 40.0)
                      for i in range(1, 121))
            + ",,,\n12,not_a_number,,\n", encoding="utf-8")
        # The same capture with a semicolon delimiter (older CapFrameX exports).
        semi = root / "semicolon.csv"
        semi.write_text(
            "TimeInSeconds;FrameTimesInMilliseconds;CPU Usage\n"
            + "".join("%d;%s;12.5\n" % (i // 60, 20.0 if i % 50 else 40.0)
                      for i in range(1, 121)), encoding="utf-8")
        b = root / "optigd_run1.csv"
        b.write_text(
            "TimeInSeconds,FrameTimesInMilliseconds,CPU Usage,GPU Usage\n"
            + "".join("%d,%s,10.0,90.0\n" % (i // 60, 16.0 if i % 50 else 33.0)
                      for i in range(1, 121)), encoding="utf-8")
        # FPS-column-only export, tab delimited, no time column.
        c = root / "fps_only.csv"
        c.write_text("FPS\tGPU Usage\n" + "".join("62.5\t90.0\n" for _ in range(60)),
                     encoding="utf-8")
        # Degenerate: a header and one usable frame is not a capture.
        d = root / "degenerate.csv"
        d.write_text("FrameTimesInMilliseconds\n16.6\n", encoding="utf-8")
        # Unreadable: no known column at all.
        e = root / "unknown.csv"
        e.write_text("CPU Usage,GPU Usage\n1,2\n3,4\n", encoding="utf-8")

        stats_a = read_capture(a)
        stats_b = read_capture(b)
        stats_c = read_capture(c)

        if stats_a["frames"] != 120:
            errors.append("blank/bad rows were not rejected cleanly: got %d frames"
                          % stats_a["frames"])
        if stats_a["rows_rejected"] != 2:
            errors.append("expected 2 rejected rows, got %d" % stats_a["rows_rejected"])
        # 118 frames at 20.0 ms plus 2 spikes at 40.0 ms -> 2440 / 120.
        if abs(stats_a["mean_frametime_ms"] - (2440.0 / 120.0)) > 1e-3:
            errors.append("mean frametime wrong: %s" % stats_a["mean_frametime_ms"])
        if abs(stats_a["one_pct_low_fps"] - 25.0) > 1e-6:
            errors.append("1%% low FPS should be the worst 2 of 120 frames (40 ms -> 25 fps),"
                          " got %s" % stats_a["one_pct_low_fps"])
        # 120 frames, p95 -> rank ceil(114) = 114 -> index 113. Sorted: 118 x 20.0
        # then 2 x 40.0, so index 113 is 20.0; p99 -> rank 119 -> index 118 = 40.0.
        if abs(stats_a["p95_frametime_ms"] - 20.0) > 1e-9:
            errors.append("p95 does not match nearest-rank: %s" % stats_a["p95_frametime_ms"])
        if abs(stats_a["p99_frametime_ms"] - 40.0) > 1e-9:
            errors.append("p99 does not match nearest-rank: %s" % stats_a["p99_frametime_ms"])
        expected_median = 20.0
        if abs(stats_a["median_frametime_ms"] - expected_median) > 1e-9:
            errors.append("median wrong: %s" % stats_a["median_frametime_ms"])
        if stats_a["delimiter"] != "','":
            errors.append("delimiter detection failed: %s" % stats_a["delimiter"])
        stats_semi = read_capture(semi)
        if stats_semi["delimiter"] != "';'" or stats_semi["frames"] != 120:
            errors.append("semicolon export was not read: delimiter %s, %d frames"
                          % (stats_semi["delimiter"], stats_semi["frames"]))
        if abs(stats_semi["mean_frametime_ms"] - stats_a["mean_frametime_ms"]) > 1e-9:
            errors.append("the same capture must read identically with ; or ,")
        if abs(stats_c["mean_frametime_ms"] - 16.0) > 1e-9:
            errors.append("FPS-only column was not converted to frametime: %s"
                          % stats_c["mean_frametime_ms"])
        if stats_c["delimiter"] != "'\\t'":
            errors.append("tab delimiter not detected: %s" % stats_c["delimiter"])
        if stats_a["one_pct_low_fps"] <= 0:
            errors.append("1% low FPS must be positive for a real capture")

        for path, why in ((d, "a single frame is not a capture"),
                          (e, "a file with no frametime column")):
            try:
                read_capture(path)
                errors.append("%s should have been refused (%s)" % (path.name, why))
            except ValueError:
                pass

        # Explicit overrides must win over detection.
        stats_override = read_capture(a, frametime_column="FrameTimesInMilliseconds")
        if stats_override["frames"] != stats_a["frames"]:
            errors.append("--frametime-column changed the result")

        text = render("vanilla", pool([stats_a]), "optigd", pool([stats_b]))
        if "No statistical significance is claimed" not in text:
            errors.append("the output must state that no significance is claimed")
        if SOURCE not in text:
            errors.append("the output must name its source")
        if "1% low FPS" not in text:
            errors.append("the output must include the 1% low FPS row")

    if errors:
        for e in errors:
            print("  [ERROR] %s" % e)
        print("compare_capframex.py self-test: FAIL")
        return 1
    print("compare_capframex.py self-test: PASS")
    print("  checks over comma/semicolon/tab exports, an FPS-only export,")
    print("  rejected rows, degenerate and unrecognized files, and the shared")
    print("  nearest-rank percentile definition.")
    print("  NOTE: the CSVs are synthetic fixtures. No CapFrameX capture was")
    print("  available in this environment; none is implied by this test.")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Compare external CapFrameX CSV captures side by side.",
        add_help=True)
    parser.add_argument("--a", nargs="+", metavar="CSV",
                        help="captures for side A (e.g. without OptiGD)")
    parser.add_argument("--b", nargs="+", metavar="CSV",
                        help="captures for side B (e.g. with OptiGD)")
    parser.add_argument("--label-a", default="A")
    parser.add_argument("--label-b", default="B")
    parser.add_argument("--delimiter", default="auto",
                        help="auto (default), comma, semicolon or tab")
    parser.add_argument("--frametime-column", default=None)
    parser.add_argument("--fps-column", default=None)
    parser.add_argument("--json", default=None, metavar="PATH",
                        help="also write the pooled numbers as JSON")
    parser.add_argument("--self-test", action="store_true",
                        help="run the built-in reader/statistics checks and exit")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()
    if not args.a or not args.b:
        parser.print_usage()
        print("FAIL: both --a and --b need at least one CSV")
        return 2

    delim = {"auto": "auto", "comma": ",", "semicolon": ";", "tab": "\t"}.get(
        args.delimiter, args.delimiter)

    sides = {}
    for label, files in ((args.label_a, args.a), (args.label_b, args.b)):
        captures = []
        for name in files:
            path = Path(name)
            if not path.exists():
                print("FAIL: capture not found: %s" % path)
                return 2
            try:
                captures.append(read_capture(path, delimiter=delim,
                                             frametime_column=args.frametime_column,
                                             fps_column=args.fps_column))
            except (ValueError, OSError) as exc:
                print("FAIL: %s" % exc)
                return 1
        sides[label] = captures

    pooled_a = pool(sides[args.label_a])
    pooled_b = pool(sides[args.label_b])
    if not pooled_a or not pooled_b:
        print("FAIL: one side produced no usable frames")
        return 1

    print("Per-capture numbers")
    print("-" * 78)
    for label, captures in ((args.label_a, sides[args.label_a]),
                            (args.label_b, sides[args.label_b])):
        for c in captures:
            print("  %-10s %-28s %5d frames  %7.1f s  mean %8.3f ms  p99 %8.3f ms"
                  "  1%% low %6.2f fps"
                  % (label, c["file"], c["frames"], c["duration_s"],
                     c["mean_frametime_ms"], c["p99_frametime_ms"], c["one_pct_low_fps"]))
            if c["rows_rejected"]:
                print("             (%d rows rejected: blank, non-numeric or non-positive)"
                      % c["rows_rejected"])
    print("")
    print(render(args.label_a, pooled_a, args.label_b, pooled_b))

    if args.json:
        payload = {
            "report_kind": "optigd_external_comparison",
            "source": SOURCE,
            "percentile_definition": "P50 = median; P95/P99 = nearest-rank (ceil(p*n)-1)",
            "significance_claimed": False,
            "sides": {
                args.label_a: {"captures": sides[args.label_a], "pooled": pooled_a},
                args.label_b: {"captures": sides[args.label_b], "pooled": pooled_b},
            },
        }
        out = Path(args.json)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        print("\nWrote %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())

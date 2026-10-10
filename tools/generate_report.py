#!/usr/bin/env python3
"""Prints a short text summary of an OptiGD diagnostics JSON file.

The JSON is written by DiagnosticsEngine::exportReportJson (mod or native harness). The
summary repeats only the stored values; it adds no measurements of its own.

Usage: generate_report.py [optigd_diagnostics.json]
"""

import json
import os
import sys


def _f(value, digits=2):
    return f"{value:.{digits}f}" if isinstance(value, (int, float)) else "n/a"


def main(argv):
    path = argv[1] if len(argv) > 1 else "optigd_diagnostics.json"
    if not os.path.exists(path):
        print(f"{path} does not exist. Enable diagnostics in the mod or run the native harness first.")
        return 0

    with open(path, "r", encoding="utf-8") as fh:
        data = json.load(fh)

    fps = data.get("fps", {})
    interval = data.get("frameIntervalMs", {})
    shadow = data.get("shadowCulling", {})

    print("=== OptiGD Diagnostics Summary ===")
    print(f"Version: {data.get('version', 'n/a')}")
    print(f"Effective tier: {data.get('effectiveTier', 'n/a')}")
    print(f"Total frames: {data.get('totalFrames', 'n/a')} (measured intervals: {data.get('measuredIntervals', 'n/a')})")
    print(f"FPS: avg {_f(fps.get('average'), 1)} (min {_f(fps.get('min'), 1)}, max {_f(fps.get('max'), 1)})")
    print(f"Frame interval: mean {_f(interval.get('mean'))} ms, p95 {_f(interval.get('p95'))} ms, "
          f"p99 {_f(interval.get('p99'))} ms, stdDev {_f(interval.get('stdDev'))} ms")
    print(f"Game update mean: {_f(data.get('gameUpdateMsMean'), 3)} ms; "
          f"OptiGD overhead mean: {_f(data.get('optigdOverheadUsMean'), 1)} us")
    print(f"Shadow culling (not applied): {shadow.get('objectsWouldCull', 'n/a')} of "
          f"{shadow.get('objectsSeen', 'n/a')} object-frames would be culled "
          f"(ratio {_f(shadow.get('ratio'), 3)})")
    print(f"Arena allocations: {data.get('arenaAllocations', 'n/a')}")
    print(f"Spikes (> 33.3 ms): {data.get('spikes', 'n/a')}")
    print(f"Fallback events: {data.get('fallbackEvents', 'n/a')}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

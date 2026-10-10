#!/usr/bin/env python3
"""Summarize and compare OptiGD frame-time CSV files.

Input: CSV files written by the mod's frame-time recorder (setting `record-frametimes`).
The header must match diagnostics/FrameTimeRecorder.cpp exactly.

Usage:
  analyze_frametimes.py summary run.csv [--warmup-frames 60] [--target-fps 60]
  analyze_frametimes.py compare baseline.csv optigd.csv [--warmup-frames 60] [--min-frames 1800]

Output is JSON on stdout. Every number comes from the input file(s). The tool does not
decide whether an optimization helped: the comparison reports differences and a bootstrap
confidence interval, and docs/GAMEPLAY_BENCHMARK_PROTOCOL.md defines what counts as evidence.

Only intervals > 0 are used (the recorder writes 0 for a discontinuity).
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import random
import statistics
import sys
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Iterable, List, Sequence

EXPECTED_HEADER = [
    "frame",
    "interval_us",
    "game_update_us",
    "optigd_overhead_us",
    "culling_shadow_us",
    "total_objects",
    "shadow_visible",
    "shadow_culled",
    "arena_allocations",
    "tier",
]

SPIKE_THRESHOLD_MS = 33.3  # matches DiagnosticsEngine::kSpikeThresholdUs (two frames at 60 fps)


class FrameTimeFormatError(ValueError):
    pass


@dataclass
class Series:
    source: str
    frames_total: int
    intervals_ms: List[float]
    overhead_us: List[float]
    tiers: List[str]


def load_csv(path: Path, warmup_frames: int) -> Series:
    with path.open(newline="", encoding="utf-8") as fh:
        reader = csv.reader(fh)
        try:
            header = next(reader)
        except StopIteration:
            raise FrameTimeFormatError(f"{path}: empty file")
        if [h.strip() for h in header] != EXPECTED_HEADER:
            raise FrameTimeFormatError(f"{path}: header does not match the recorder format")

        intervals: List[float] = []
        overhead: List[float] = []
        tiers: List[str] = []
        total = 0
        for lineno, row in enumerate(reader, start=2):
            if not row:
                continue
            if len(row) != len(EXPECTED_HEADER):
                raise FrameTimeFormatError(f"{path}:{lineno}: expected {len(EXPECTED_HEADER)} columns")
            total += 1
            if int(row[0]) < warmup_frames:
                continue
            interval_us = float(row[1])
            if not math.isfinite(interval_us) or interval_us <= 0.0:
                continue
            intervals.append(interval_us / 1000.0)
            overhead.append(float(row[3]))
            tiers.append(row[9])
    return Series(str(path), total, intervals, overhead, tiers)


def percentile(sorted_values: Sequence[float], p: float) -> float:
    """Nearest-rank percentile, the same rule used by the mod (ceil(p*n) - 1)."""
    if not sorted_values:
        raise ValueError("percentile of empty sequence")
    n = len(sorted_values)
    rank = max(1, math.ceil(p * n))
    return sorted_values[min(rank, n) - 1]


def summarize(series: Series, target_fps: float) -> dict:
    values = sorted(series.intervals_ms)
    if not values:
        raise ValueError(f"{series.source}: no usable frame intervals after warm-up")
    budget_ms = 1000.0 / target_fps
    mean_ms = statistics.fmean(values)
    return {
        "source": series.source,
        "framesTotal": series.frames_total,
        "framesUsed": len(values),
        "meanFrameMs": mean_ms,
        "medianFrameMs": statistics.median(values),
        "p95FrameMs": percentile(values, 0.95),
        "p99FrameMs": percentile(values, 0.99),
        "stdDevFrameMs": statistics.pstdev(values),
        "meanFps": 1000.0 / mean_ms,
        "spikesOver33ms": sum(1 for v in values if v > SPIKE_THRESHOLD_MS),
        "framesOverBudget": sum(1 for v in values if v > budget_ms),
        "targetFps": target_fps,
        "meanOptigdOverheadUs": statistics.fmean(series.overhead_us) if series.overhead_us else 0.0,
        "tiersSeen": sorted(set(series.tiers)),
        "status": "measured",
    }


def bootstrap_mean_diff_ci(a: Sequence[float], b: Sequence[float], iterations: int, seed: int) -> dict:
    """95% percentile bootstrap CI for mean(b) - mean(a). Deterministic for a fixed seed."""
    rng = random.Random(seed)
    diffs = []
    na, nb = len(a), len(b)
    for _ in range(iterations):
        ma = statistics.fmean(rng.choice(a) for _ in range(na))
        mb = statistics.fmean(rng.choice(b) for _ in range(nb))
        diffs.append(mb - ma)
    diffs.sort()
    lo = diffs[int(0.025 * (iterations - 1))]
    hi = diffs[int(0.975 * (iterations - 1))]
    return {"lowMs": lo, "highMs": hi, "iterations": iterations, "seed": seed, "level": 0.95}


def compare(baseline: Series, candidate: Series, target_fps: float, min_frames: int, iterations: int) -> dict:
    for s in (baseline, candidate):
        if len(s.intervals_ms) < min_frames:
            raise ValueError(f"{s.source}: {len(s.intervals_ms)} usable frames, need at least {min_frames}")
    sb = summarize(baseline, target_fps)
    sc = summarize(candidate, target_fps)
    return {
        "baseline": sb,
        "candidate": sc,
        "deltaMeanFrameMs": sc["meanFrameMs"] - sb["meanFrameMs"],
        "deltaMeanFpsPercent": (sc["meanFps"] / sb["meanFps"] - 1.0) * 100.0,
        "deltaP95FrameMs": sc["p95FrameMs"] - sb["p95FrameMs"],
        "meanFrameDiffCI95": bootstrap_mean_diff_ci(baseline.intervals_ms, candidate.intervals_ms, iterations, seed=1),
        "note": "Differences are only meaningful for the same level, route, device, and settings "
                "(see docs/GAMEPLAY_BENCHMARK_PROTOCOL.md).",
    }


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)

    s = sub.add_parser("summary", help="summarize one run")
    s.add_argument("csv", type=Path)
    s.add_argument("--warmup-frames", type=int, default=60)
    s.add_argument("--target-fps", type=float, default=60.0)

    c = sub.add_parser("compare", help="compare a baseline and a candidate run")
    c.add_argument("baseline", type=Path)
    c.add_argument("candidate", type=Path)
    c.add_argument("--warmup-frames", type=int, default=60)
    c.add_argument("--target-fps", type=float, default=60.0)
    c.add_argument("--min-frames", type=int, default=1800)
    c.add_argument("--bootstrap-iterations", type=int, default=2000)
    return p


def main(argv: Iterable[str] | None = None) -> int:
    args = build_parser().parse_args(list(argv) if argv is not None else None)
    if args.target_fps <= 0:
        print("error: --target-fps must be positive", file=sys.stderr)
        return 2
    try:
        if args.command == "summary":
            result = summarize(load_csv(args.csv, args.warmup_frames), args.target_fps)
        else:
            base = load_csv(args.baseline, args.warmup_frames)
            cand = load_csv(args.candidate, args.warmup_frames)
            result = compare(base, cand, args.target_fps, args.min_frames, args.bootstrap_iterations)
    except (OSError, FrameTimeFormatError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

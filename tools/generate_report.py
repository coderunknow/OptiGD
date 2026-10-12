#!/usr/bin/env python3
"""
OptiGD v0.1.0-beta.3 Diagnostics & Benchmark Report Formatter
Parses JSON reports exported by DiagnosticsEngine or BenchmarkSuite and produces
human-readable Markdown tables, including before/after baseline vs. OptiGD comparisons.

Two honesty rules shape the output:

  * Every number is rendered with the **window it came from**. A v2 report states
    how many samples its percentiles used, whether the history was truncated, and
    whether the percentiles are exact or histogram-approximate (rendered with a
    `~`). A v1 report states none of that, so the output says the window is unknown
    rather than implying the numbers are exact.
  * `--compare` reports **whether the two sides are comparable at all** before it
    reports any delta, and never claims statistical significance. This is the
    legacy path; Benchmark Mode's in-game comparator
    (`src/benchmark/BenchmarkCompare.cpp`) is stricter and is the one to use for
    A/B work.
"""

import json
import sys
from pathlib import Path


def window_facts(data: dict) -> dict:
    """Collect what a report discloses about the window its numbers came from."""
    tel = data.get("telemetry", {}) if isinstance(data.get("telemetry"), dict) else {}
    version = data.get("report_schema_version", 1)
    method = tel.get("percentile_method")
    return {
        "schema_version": version,
        "disclosed": version >= 2,
        "total_frames": tel.get("total_frames", 0),
        "sample_count": tel.get("sample_count"),
        "sample_window_ms": tel.get("sample_window_ms"),
        "percentile_method": method,
        "approximate": method not in (None, "exact"),
        "history_capacity": tel.get("history_capacity"),
        "history_truncated": tel.get("history_truncated"),
        "excluded_samples": tel.get("excluded_samples"),
        "rejected_samples": tel.get("rejected_samples"),
        "stall_frames": tel.get("stall_frames"),
        "paused_frames": tel.get("paused_frames"),
        "thread_cpu_available": tel.get("thread_cpu_time_available"),
        "classification": data.get("evidence_classification", "[UNSTATED]"),
        "platform": (data.get("platform") or {}).get("os", "Unknown"),
        "architecture": (data.get("platform") or {}).get("architecture")
                        or (data.get("platform") or {}).get("arch"),
    }


def tilde(facts: dict) -> str:
    """Marks a percentile as approximate, the same way the in-game panel does."""
    return "~" if facts["approximate"] else ""


CLASSIFICATION_MEANING = {
    "[MEASURED-IN-GAME]": "OptiGD's own instrumentation inside a live Geometry Dash "
                          "process; not an external presented-frame measurement",
    "[INTERNAL-TELEMETRY]": "OptiGD's own instrumentation; not presented-frame timing",
    "[SIMULATED]": "synthetic harness output - not a measurement of Geometry Dash, "
                   "and no live-game result is implied",
}


def format_window_section(facts: dict) -> list:
    lines = ["## Measurement Window\n"]
    meaning = CLASSIFICATION_MEANING.get(facts["classification"],
                                         "classification not recognized by this tool")
    lines.append(f"- **Evidence classification**: `{facts['classification']}` ({meaning})")
    if facts["sample_count"] is None and not facts["total_frames"]:
        lines.append("- **Window disclosure**: not applicable - this artifact is a "
                     "synthetic benchmark-suite report (`scenarios`), not a frame-time "
                     "capture. It has no measurement window and its numbers are not "
                     "Geometry Dash frame times.")
        return lines
    if not facts["disclosed"]:
        lines.append("- **Window disclosure**: *unknown* - this is a v1 report, which "
                     "predates window disclosure. Its percentiles may describe a "
                     "different span than its `total_frames`, and the method "
                     "(exact vs. histogram-approximate) is not stated.")
        return lines
    lines.append(f"- **Samples behind the percentiles**: {facts['sample_count']} of "
                 f"{facts['total_frames']} frames offered")
    if facts["sample_window_ms"] is not None:
        lines.append(f"- **Window length**: {facts['sample_window_ms']:.1f} ms of counted frame time")
    lines.append(f"- **Percentile method**: `{facts['percentile_method']}`"
                 + (" (values below are marked `~`)" if facts["approximate"] else ""))
    if facts["history_capacity"] is not None:
        lines.append(f"- **History**: capacity {facts['history_capacity']:,}"
                     f"{', TRUNCATED' if facts['history_truncated'] else ', complete'}")
    lines.append(f"- **Excluded frames**: {facts['excluded_samples']} "
                 f"({facts['stall_frames']} stalls, {facts['paused_frames']} paused)"
                 f" | **rejected**: {facts['rejected_samples']}")
    if facts["thread_cpu_available"]:
        lines.append("- **Thread CPU time**: available (reported separately from the "
                     "wall-clock span of the update call)")
    else:
        lines.append("- **Thread CPU time**: not available on this platform; the "
                     "reported value is 0, not a measurement of zero")
    return lines


def format_single_report(data: dict) -> str:
    lines = []
    version = data.get("optigd_version", data.get("version", "0.1.0-beta.3"))
    lines.append(f"# OptiGD v{version} Diagnostics Summary\n")

    facts = window_facts(data)
    if "platform" in data:
        plat = data["platform"]
        arch = plat.get("architecture") or plat.get("arch") or "architecture unstated"
        lines.append(
            f"- **Platform**: {plat.get('os', 'Unknown')} ({arch}), "
            f"{plat.get('cores', plat.get('logical_cores', 0))} Logical Cores"
        )
    if "gd_version" in data or "geode_version" in data:
        lines.append(
            f"- **Runtime Target**: Geometry Dash `{data.get('gd_version', '2.2081')}` | "
            f"Geode `{data.get('geode_version', 'v5.10.1')}`"
        )

    if "configuration" in data:
        cfg = data["configuration"]
        lines.append(
            f"- **Active Tier**: `{cfg.get('active_tier', 'Unknown')}` "
            f"(Target FPS: {cfg.get('target_fps', 60)})"
        )
        lines.append(
            f"- **Particle Density Scale**: {float(cfg.get('particle_density_scale', 0.5)) * 100:.0f}% "
            f"(Cap: {cfg.get('max_active_particles', 2048)})\n"
        )

    lines.extend(format_window_section(facts))
    lines.append("")

    if "telemetry" in data:
        tel = data["telemetry"]
        t = tilde(facts)
        lines.append("## Frame Telemetry\n")
        lines.append("| Metric | Value |")
        lines.append("|---|---|")
        lines.append(f"| Total Frames (offered) | {tel.get('total_frames', 0)} |")
        if facts["disclosed"]:
            lines.append(f"| Samples Behind Percentiles | {facts['sample_count']} |")
        lines.append(f"| Average FPS | {tel.get('avg_fps', 0):.2f} |")
        lines.append(f"| Min / Max FPS | {tel.get('min_fps', 0):.2f} / {tel.get('max_fps', 0):.2f} |")
        lines.append(f"| Mean Frame Time | {tel.get('avg_frame_time_ms', 0):.3f} ms |")
        lines.append(f"| Median (P50) Frame Time | {tel.get('median_frame_time_ms', 0):.3f}{t} ms |")
        lines.append(f"| P95 Frame Time | {tel.get('p95_frame_time_ms', 0):.3f}{t} ms |")
        lines.append(f"| P99 Frame Time | {tel.get('p99_frame_time_ms', 0):.3f}{t} ms |")
        lines.append(f"| Mean Update Wall Time | {tel.get('avg_update_cpu_time_ms', 0):.3f} ms |")
        if facts["disclosed"]:
            lines.append(f"| Mean Update Thread CPU Time | "
                         f"{tel.get('avg_update_thread_cpu_time_ms', 0):.3f} ms"
                         f"{'' if facts['thread_cpu_available'] else ' (unavailable)'} |")
        lines.append(f"| Mean OptiGD Overhead | {tel.get('avg_optigd_overhead_us', 0):.2f} us |")
        lines.append(f"| Objects Culled | {tel.get('total_objects_culled', 0):,} |")
        lines.append(f"| Particles Avoided | {tel.get('total_particles_avoided', 0):,} |")
        lines.append(f"| Triggers Skipped | {tel.get('total_triggers_skipped', 0):,} |")
        lines.append(
            f"| Matrix Cache Hits / Misses | {tel.get('total_matrix_cache_hits', 0):,} / "
            f"{tel.get('total_matrix_cache_misses', 0):,} |"
        )
        lines.append(f"| Peak Memory Arena | {tel.get('peak_arena_bytes', 0):,} bytes |")
        lines.append(f"| Total Arena Allocations | {tel.get('total_arena_allocations', 0):,} |")
        lines.append(f"| Heap Allocations Avoided | {tel.get('total_heap_allocations_avoided', 0):,} |")
        lines.append(f"| Adaptive Tier Transitions | {tel.get('tier_transitions_count', 0)} |\n")

    if "fallbacks" in data and data["fallbacks"]:
        lines.append("## Triggered Safety Fallbacks\n")
        for fb in data["fallbacks"]:
            feat = fb.get("feature", "subsystem")
            lines.append(
                f"- **Frame {fb.get('frame')}** [`{feat}`]: {fb.get('reason')} -> *{fb.get('action')}*"
            )

    return "\n".join(lines)


def pct_delta(baseline: float, optimized: float) -> str:
    if baseline == 0:
        return "N/A"
    delta = ((optimized - baseline) / baseline) * 100.0
    sign = "+" if delta > 0 else ""
    return f"{sign}{delta:.2f}%"


def comparability(base_facts: dict, opti_facts: dict) -> tuple:
    """Returns (blocking, warnings) about whether these two reports may be compared."""
    blocking, warnings = [], []

    for label, facts in (("baseline", base_facts), ("optimized", opti_facts)):
        if not facts["sample_count"] and not facts["total_frames"]:
            blocking.append(f"the {label} report contains no frames at all")
        elif facts["disclosed"] and not facts["sample_count"]:
            blocking.append(f"the {label} report counted zero samples")

    if not base_facts["disclosed"] or not opti_facts["disclosed"]:
        warnings.append("at least one side is a v1 report, so the window its "
                        "percentiles came from is unknown")
    if base_facts["platform"] != opti_facts["platform"] or \
            (base_facts["architecture"] and opti_facts["architecture"] and
             base_facts["architecture"] != opti_facts["architecture"]):
        blocking.append("the two sides were captured on different platforms or "
                        f"architectures ({base_facts['platform']}/"
                        f"{base_facts['architecture']} vs {opti_facts['platform']}/"
                        f"{opti_facts['architecture']})")
    if base_facts["approximate"] or opti_facts["approximate"]:
        warnings.append("at least one side has histogram-approximate percentiles "
                        "(its sample history overflowed)")
    if base_facts["history_truncated"] or opti_facts["history_truncated"]:
        warnings.append("at least one side exceeded its history capacity, so its "
                        "percentiles describe a shorter span than its frame count")

    a, b = base_facts["sample_window_ms"], opti_facts["sample_window_ms"]
    if a and b:
        gap = abs(a - b) / max(a, b)
        if gap > 0.25:
            blocking.append(f"the measured windows differ by {gap * 100:.0f}% "
                            f"({a:.0f} ms vs {b:.0f} ms): these are not the same experiment")
        elif gap > 0.10:
            warnings.append(f"the measured windows differ by {gap * 100:.1f}% "
                            f"({a:.0f} ms vs {b:.0f} ms)")

    for label, facts in (("baseline", base_facts), ("optimized", opti_facts)):
        if facts["paused_frames"] or facts["stall_frames"]:
            warnings.append(f"the {label} side excluded {facts['stall_frames']} stalled and "
                            f"{facts['paused_frames']} paused frames: part of the elapsed "
                            "time was not gameplay")
    return blocking, warnings


def format_comparison_report(base_data: dict, opti_data: dict) -> str:
    bt = base_data.get("telemetry", {})
    ot = opti_data.get("telemetry", {})
    version = opti_data.get("optigd_version", "0.1.0-beta.3")
    bf = window_facts(base_data)
    of = window_facts(opti_data)
    blocking, warnings = comparability(bf, of)
    bt_t, ot_t = tilde(bf), tilde(of)

    lines = [
        f"# OptiGD v{version} Comparative Telemetry Report\n",
        f"- **Source of every number**: `{bf['classification']}` - OptiGD's own "
        "in-game instrumentation. This is NOT an external presented-frame "
        "measurement; for that see the CapFrameX procedure in docs/BENCHMARKS.md.",
        f"- **Baseline window**: {_window_summary(bf)}",
        f"- **Optimized window**: {_window_summary(of)}",
    ]
    if blocking:
        lines.append("")
        lines.append("> **These two reports should NOT be compared.**")
        for item in blocking:
            lines.append(f"> - {item}")
    if warnings:
        lines.append("")
        lines.append("**Comparability warnings**")
        for item in warnings:
            lines.append(f"- {item}")
    lines.append("")
    lines += [
        "| Metric | Baseline (Passthrough) | OptiGD Enabled | Delta |",
        "|---|---|---|---|",
        f"| Samples Behind Percentiles | {_num(bf['sample_count'])} | {_num(of['sample_count'])} | - |",
        f"| Average FPS | {bt.get('avg_fps', 0):.2f} | {ot.get('avg_fps', 0):.2f} | **{pct_delta(bt.get('avg_fps', 0), ot.get('avg_fps', 0))}** |",
        f"| Mean Frame Time (ms) | {bt.get('avg_frame_time_ms', 0):.3f} ms | {ot.get('avg_frame_time_ms', 0):.3f} ms | **{pct_delta(bt.get('avg_frame_time_ms', 0), ot.get('avg_frame_time_ms', 0))}** |",
        f"| Median P50 Frame Time (ms) | {bt.get('median_frame_time_ms', 0):.3f}{bt_t} ms | {ot.get('median_frame_time_ms', 0):.3f}{ot_t} ms | **{pct_delta(bt.get('median_frame_time_ms', 0), ot.get('median_frame_time_ms', 0))}** |",
        f"| P95 Frame Time (ms) | {bt.get('p95_frame_time_ms', 0):.3f}{bt_t} ms | {ot.get('p95_frame_time_ms', 0):.3f}{ot_t} ms | **{pct_delta(bt.get('p95_frame_time_ms', 0), ot.get('p95_frame_time_ms', 0))}** |",
        f"| P99 Frame Time (ms) | {bt.get('p99_frame_time_ms', 0):.3f}{bt_t} ms | {ot.get('p99_frame_time_ms', 0):.3f}{ot_t} ms | **{pct_delta(bt.get('p99_frame_time_ms', 0), ot.get('p99_frame_time_ms', 0))}** |",
        f"| Mean Update Wall Time (ms) | {bt.get('avg_update_cpu_time_ms', 0):.3f} ms | {ot.get('avg_update_cpu_time_ms', 0):.3f} ms | **{pct_delta(bt.get('avg_update_cpu_time_ms', 0), ot.get('avg_update_cpu_time_ms', 0))}** |",
        f"| Mean Update Thread CPU Time (ms) | {_thread(bt, bf)} | {_thread(ot, of)} | **{pct_delta(bt.get('avg_update_thread_cpu_time_ms', 0), ot.get('avg_update_thread_cpu_time_ms', 0))}** |",
        f"| Objects Culled | {bt.get('total_objects_culled', 0):,} | {ot.get('total_objects_culled', 0):,} | +{ot.get('total_objects_culled', 0) - bt.get('total_objects_culled', 0):,} |",
        f"| Particles Avoided | {bt.get('total_particles_avoided', 0):,} | {ot.get('total_particles_avoided', 0):,} | +{ot.get('total_particles_avoided', 0) - bt.get('total_particles_avoided', 0):,} |",
        f"| Heap Allocations Avoided | {bt.get('total_heap_allocations_avoided', 0):,} | {ot.get('total_heap_allocations_avoided', 0):,} | +{ot.get('total_heap_allocations_avoided', 0) - bt.get('total_heap_allocations_avoided', 0):,} |",
    ]
    if blocking:
        lines.append("")
        lines.append("_The deltas above are printed for completeness. With a blocking "
                     "comparability problem they do not describe a difference between "
                     "two runs of the same experiment, and must not be quoted._")
    lines.append("")
    lines.append("**No statistical significance is claimed or computed.** This is one "
                 "pair of internal telemetry reports. A single pair cannot separate an "
                 "optimization effect from run-to-run variability: capture at least "
                 "3-5 pairs, alternating the order, and compare the spread (see the "
                 "repeated-run protocol in docs/BENCHMARKS.md and in Benchmark Mode's "
                 "Protocol view).")
    lines.append("")
    lines.append("`~` marks a percentile that is histogram-approximate rather than "
                 "exact. `N/A` marks a delta from a zero baseline, which is undefined "
                 "rather than infinitely large.")
    return "\n".join(lines)


def _num(value) -> str:
    return "unknown (v1 report)" if value is None else f"{value:,}"


def _thread(tel: dict, facts: dict) -> str:
    if not facts["disclosed"]:
        return "unknown (v1 report)"
    value = tel.get("avg_update_thread_cpu_time_ms", 0)
    return f"{value:.3f} ms" if facts["thread_cpu_available"] else "unavailable"


def _window_summary(facts: dict) -> str:
    if not facts["disclosed"]:
        return "unknown - v1 report, no window disclosure"
    parts = [f"{facts['sample_count']} of {facts['total_frames']} frames"]
    if facts["sample_window_ms"] is not None:
        parts.append(f"{facts['sample_window_ms']:.0f} ms")
    parts.append(f"percentiles `{facts['percentile_method']}`")
    if facts["excluded_samples"]:
        parts.append(f"{facts['excluded_samples']} excluded "
                     f"({facts['stall_frames']} stall / {facts['paused_frames']} paused)")
    return ", ".join(parts)


def main():
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python3 generate_report.py <optigd_diagnostics.json>")
        print("  python3 generate_report.py --compare <baseline.json> <optigd.json>")
        sys.exit(1)

    if sys.argv[1] == "--compare":
        if len(sys.argv) < 4:
            print("Error: --compare requires <baseline.json> and <optigd.json>")
            sys.exit(1)
        base_path = Path(sys.argv[2])
        opti_path = Path(sys.argv[3])
        if not base_path.exists() or not opti_path.exists():
            print("Error: comparison file not found.")
            sys.exit(1)
        with open(base_path, "r", encoding="utf-8") as f:
            base_data = json.load(f)
        with open(opti_path, "r", encoding="utf-8") as f:
            opti_data = json.load(f)
        print(format_comparison_report(base_data, opti_data))
        return

    path = Path(sys.argv[1])
    if not path.exists():
        print(f"Error: File '{path}' not found.")
        sys.exit(1)

    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)

    print(format_single_report(data))


if __name__ == "__main__":
    main()

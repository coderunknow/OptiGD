#!/usr/bin/env python3
"""
OptiGD v0.1.0-beta.2 Diagnostics & Benchmark Report Formatter
Parses JSON reports exported by DiagnosticsEngine or BenchmarkSuite and produces
human-readable Markdown tables, including before/after baseline vs. OptiGD comparisons.
"""

import json
import sys
from pathlib import Path


def format_single_report(data: dict) -> str:
    lines = []
    version = data.get("optigd_version", data.get("version", "0.1.0-beta.2"))
    lines.append(f"# OptiGD v{version} Diagnostics Summary\n")

    if "platform" in data:
        plat = data["platform"]
        lines.append(
            f"- **Platform**: {plat.get('os', 'Unknown')} ({plat.get('arch', 'Unknown')}), "
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

    if "telemetry" in data:
        tel = data["telemetry"]
        lines.append("## Frame Telemetry\n")
        lines.append("| Metric | Value |")
        lines.append("|---|---|")
        lines.append(f"| Total Frames | {tel.get('total_frames', 0)} |")
        lines.append(f"| Average FPS | {tel.get('avg_fps', 0):.2f} |")
        lines.append(f"| Min / Max FPS | {tel.get('min_fps', 0):.2f} / {tel.get('max_fps', 0):.2f} |")
        lines.append(f"| Mean Frame Time | {tel.get('avg_frame_time_ms', 0):.3f} ms |")
        lines.append(f"| Median (P50) Frame Time | {tel.get('median_frame_time_ms', 0):.3f} ms |")
        lines.append(f"| P95 Frame Time | {tel.get('p95_frame_time_ms', 0):.3f} ms |")
        lines.append(f"| P99 Frame Time | {tel.get('p99_frame_time_ms', 0):.3f} ms |")
        lines.append(f"| Mean Update CPU Time | {tel.get('avg_update_cpu_time_ms', 0):.3f} ms |")
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


def format_comparison_report(base_data: dict, opti_data: dict) -> str:
    bt = base_data.get("telemetry", {})
    ot = opti_data.get("telemetry", {})
    version = opti_data.get("optigd_version", "0.1.0-beta.2")

    lines = [
        f"# OptiGD v{version} Comparative Telemetry Report\n",
        "| Metric | Baseline (Passthrough) | OptiGD Enabled | Delta |",
        "|---|---|---|---|",
        f"| Average FPS | {bt.get('avg_fps', 0):.2f} | {ot.get('avg_fps', 0):.2f} | **{pct_delta(bt.get('avg_fps', 0), ot.get('avg_fps', 0))}** |",
        f"| Mean Frame Time (ms) | {bt.get('avg_frame_time_ms', 0):.3f} ms | {ot.get('avg_frame_time_ms', 0):.3f} ms | **{pct_delta(bt.get('avg_frame_time_ms', 0), ot.get('avg_frame_time_ms', 0))}** |",
        f"| Median P50 Frame Time (ms) | {bt.get('median_frame_time_ms', 0):.3f} ms | {ot.get('median_frame_time_ms', 0):.3f} ms | **{pct_delta(bt.get('median_frame_time_ms', 0), ot.get('median_frame_time_ms', 0))}** |",
        f"| P95 Frame Time (ms) | {bt.get('p95_frame_time_ms', 0):.3f} ms | {ot.get('p95_frame_time_ms', 0):.3f} ms | **{pct_delta(bt.get('p95_frame_time_ms', 0), ot.get('p95_frame_time_ms', 0))}** |",
        f"| P99 Frame Time (ms) | {bt.get('p99_frame_time_ms', 0):.3f} ms | {ot.get('p99_frame_time_ms', 0):.3f} ms | **{pct_delta(bt.get('p99_frame_time_ms', 0), ot.get('p99_frame_time_ms', 0))}** |",
        f"| Mean Update CPU Time (ms) | {bt.get('avg_update_cpu_time_ms', 0):.3f} ms | {ot.get('avg_update_cpu_time_ms', 0):.3f} ms | **{pct_delta(bt.get('avg_update_cpu_time_ms', 0), ot.get('avg_update_cpu_time_ms', 0))}** |",
        f"| Objects Culled | {bt.get('total_objects_culled', 0):,} | {ot.get('total_objects_culled', 0):,} | +{ot.get('total_objects_culled', 0) - bt.get('total_objects_culled', 0):,} |",
        f"| Particles Avoided | {bt.get('total_particles_avoided', 0):,} | {ot.get('total_particles_avoided', 0):,} | +{ot.get('total_particles_avoided', 0) - bt.get('total_particles_avoided', 0):,} |",
        f"| Heap Allocations Avoided | {bt.get('total_heap_allocations_avoided', 0):,} | {ot.get('total_heap_allocations_avoided', 0):,} | +{ot.get('total_heap_allocations_avoided', 0) - bt.get('total_heap_allocations_avoided', 0):,} |",
    ]
    return "\n".join(lines)


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

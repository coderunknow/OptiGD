#!/usr/bin/env python3
"""
OptiGD Diagnostics & Evidence Report Generator
Analyzes benchmark logs and local diagnostics summaries.
"""

import sys
import json
import os

def main():
    report_file = sys.argv[1] if len(sys.argv) > 1 else "optigd_diagnostics.json"
    if not os.path.exists(report_file):
        print(f"Diagnostics file {report_file} does not exist yet. Run OptiGD or optigd_benchmark first.")
        return 0

    with open(report_file, "r") as f:
        data = json.load(f)

    print("=== OptiGD Diagnostics Summary ===")
    print(f"Active Tier: {data.get('tier')}")
    print(f"Total Frames: {data.get('totalFrames')}")
    fps = data.get("fps", {})
    print(f"Average FPS: {fps.get('average'):.1f} (min: {fps.get('min'):.1f}, max: {fps.get('max'):.1f})")
    ft = data.get("frameTimeMs", {})
    print(f"Frame-Time: {ft.get('mean'):.2f} ms (p95: {ft.get('p95'):.2f} ms, stdDev: {ft.get('stdDev'):.2f} ms)")
    culling = data.get("culling", {})
    print(f"Culling: {culling.get('cullRatioPercent'):.1f}% culled ({culling.get('totalCulled')}/{culling.get('totalSeen')})")
    print(f"Allocations Avoided: {data.get('allocationsAvoided')}")
    print(f"Spikes Detected: {data.get('spikesDetected')}")
    print(f"Fallbacks Triggered: {data.get('fallbacksTriggered')}")
    return 0

if __name__ == "__main__":
    sys.exit(main())

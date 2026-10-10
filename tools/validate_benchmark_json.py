#!/usr/bin/env python3
"""
OptiGD synthetic benchmark JSON schema validator.

Validates the JSON emitted by `optigd_benchmark`
(`BenchmarkSuite::formatJsonReport`). Benchmarks are [SIMULATED]: this script
only checks structural integrity and the evidence classification so that a
synthetic result can never be mistaken for a real in-game measurement.

Usage:
    python3 tools/validate_benchmark_json.py benchmark_results.json
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

TOP_LEVEL = {
    "benchmark_suite": str,
    "optigd_version": str,
    "benchmark_mode": str,
    "evidence_classification": str,
    "platform": dict,
    "scenarios": list,
}

PLATFORM = {
    "os": str,
    "compiler": str,
    "logical_cores": int,
}

SCENARIO = {
    "name": str,
    "objects": int,
    "emitters": int,
    "triggers": int,
    "frames": int,
    "baseline_mean_us": (int, float),
    "baseline_median_us": (int, float),
    "baseline_p95_us": (int, float),
    "baseline_p99_us": (int, float),
    "baseline_heap_allocations": int,
    "optigd_mean_us": (int, float),
    "optigd_median_us": (int, float),
    "optigd_p95_us": (int, float),
    "optigd_p99_us": (int, float),
    "optigd_heap_allocations": int,
    "optigd_arena_allocations": int,
    "speedup_factor": (int, float),
    "cpu_reduction_percent": (int, float),
}

EXPECTED_MODE = "synthetic_stress_harness"
EXPECTED_CLASSIFICATION = "[SIMULATED]"


def validate(doc) -> list:
    errors: list = []
    if not isinstance(doc, dict):
        return ["benchmark report root must be a JSON object"]

    for key, expected in TOP_LEVEL.items():
        if key not in doc:
            errors.append("top level key '%s' is missing" % key)
        elif not isinstance(doc[key], expected):
            errors.append("top level key '%s' has type %s, expected %s"
                          % (key, type(doc[key]).__name__,
                             getattr(expected, "__name__", expected)))
    if errors:
        return errors

    if doc["evidence_classification"] != EXPECTED_CLASSIFICATION:
        errors.append("evidence_classification must be '%s' (got '%s')"
                      % (EXPECTED_CLASSIFICATION, doc["evidence_classification"]))
    if doc["benchmark_mode"] != EXPECTED_MODE:
        errors.append("benchmark_mode must be '%s' (got '%s')"
                      % (EXPECTED_MODE, doc["benchmark_mode"]))

    for key, expected in PLATFORM.items():
        if key not in doc["platform"]:
            errors.append("platform.%s is missing" % key)
        elif not isinstance(doc["platform"][key], expected):
            errors.append("platform.%s has type %s" % (key,
                                                       type(doc["platform"][key]).__name__))

    if not doc["scenarios"]:
        errors.append("scenarios array must not be empty")

    for i, scenario in enumerate(doc["scenarios"]):
        if not isinstance(scenario, dict):
            errors.append("scenarios[%d] must be an object" % i)
            continue
        for key, expected in SCENARIO.items():
            if key not in scenario:
                errors.append("scenarios[%d].%s is missing" % (i, key))
            elif not isinstance(scenario[key], expected):
                errors.append("scenarios[%d].%s has type %s, expected %s"
                              % (i, key, type(scenario[key]).__name__,
                                 getattr(expected, "__name__", expected)))
        if errors:
            continue
        if scenario["frames"] <= 0:
            errors.append("scenarios[%d].frames must be positive" % i)
        if scenario["optigd_mean_us"] < 0 or scenario["baseline_mean_us"] < 0:
            errors.append("scenarios[%d] has negative mean timings" % i)
        # OptiGD's per-frame path must not allocate on the heap.
        if scenario["optigd_heap_allocations"] != 0:
            errors.append("scenarios[%d].optigd_heap_allocations must be 0 (arena-backed)"
                          % i)
        if scenario["optigd_p95_us"] < scenario["optigd_median_us"]:
            errors.append("scenarios[%d] P95 is below the median" % i)

    return errors


def main(argv):
    if len(argv) < 2:
        print("Usage: python3 tools/validate_benchmark_json.py <benchmark_results.json>")
        return 2

    path = Path(argv[1])
    if not path.exists():
        print("FAIL: benchmark report not found: %s" % path)
        return 2

    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        print("FAIL: %s is not valid JSON: %s" % (path, exc))
        return 1

    errors = validate(doc)
    print("OptiGD synthetic benchmark JSON schema validation")
    print("  file              : %s" % path)
    print("  version           : %s" % doc.get("optigd_version"))
    print("  classification    : %s" % doc.get("evidence_classification"))
    print("  scenarios         : %d" % len(doc.get("scenarios", [])))
    for e in errors:
        print("  [ERROR] %s" % e)
    print("  result            : %s" % ("FAIL" if errors else "PASS"))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

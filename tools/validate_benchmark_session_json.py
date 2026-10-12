#!/usr/bin/env python3
"""
OptiGD benchmark session JSON schema validator.

Validates the records written by `BenchmarkSession::toJson()` - the files a
Benchmark Mode capture leaves in
`<GD save dir>/geode/mods/coderunknow.optigd/benchmarks/`, plus the copies that
`optigd_tests --emit-sessions <dir>` writes for the `OptiGDBenchmarkSessionJson`
ctest.

The record is serialized by hand-rolled C++ (`core::json`), so this script exists
to prove out of process that the artifact is parseable, that every documented
field is present with the right type, and that the numbers inside it agree with
each other. The semantic checks are the point: a session that says `completed`
with no samples, percentiles that are out of order, a frame budget that does not
add up, or a record that claims `[MEASURED-IN-GAME]` while being emitted by a
standalone binary are all failures here, not warnings.

Usage:
    python3 tools/validate_benchmark_session_json.py <session.json>
    python3 tools/validate_benchmark_session_json.py <directory>
    python3 tools/validate_benchmark_session_json.py <directory> --allow-in-game

A single file is validated as one session (or as an index, if it is one). A
directory is validated as an emitted artifact set: every session and index below
it, and - because `--emit-sessions` runs without a Geometry Dash process - every
session must be classified `[SIMULATED]`. Pass `--allow-in-game` when validating
a directory of records captured in the real game.

Exit codes: 0 = PASS, 1 = validation failures, 2 = usage / IO error.
"""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path

REPORT_KIND = "optigd_benchmark_session"
INDEX_KIND = "optigd_benchmark_index"
SCHEMA_VERSION = 1

MEASURED_IN_GAME = "[MEASURED-IN-GAME]"
SIMULATED = "[SIMULATED]"
INTERNAL_TELEMETRY = "[INTERNAL-TELEMETRY]"
KNOWN_CLASSIFICATIONS = (MEASURED_IN_GAME, SIMULATED, INTERNAL_TELEMETRY)

MODES = ("baseline", "optimized")
# Only terminal states are ever written to disk: a running session has no record.
STATES = ("completed", "incomplete", "cancelled", "invalid")

TOP_LEVEL = {
    "report_kind": str,
    "schema_version": int,
    "optigd_version": str,
    "gd_version": str,
    "geode_version": str,
    "evidence_classification": str,
    "evidence_classification_note": str,
    "session": dict,
    "platform": dict,
    "level": dict,
    "configuration": dict,
    "telemetry": dict,
    "integrity": dict,
    "measurement_definitions": dict,
    "limitations": list,
}

SESSION = {
    "id": str,
    "mode": str,
    "mode_display": str,
    "state": str,
    "status": str,
    "comparable": bool,
    "started_utc": str,
    "ended_utc": str,
    "started_epoch_ms": int,
    "ended_epoch_ms": int,
    "target_duration_ms": (int, float),
    "captured_duration_ms": (int, float),
    "wall_duration_ms": (int, float),
    "frames_observed": int,
    "frames_counted": int,
    "frames_excluded": int,
    "frames_rejected": int,
}

PLATFORM = {
    "os": str,
    "architecture": str,
    "compiler": str,
    "cores": int,
    "total_ram_mb": int,
    "refresh_hint": str,
}

LEVEL = {
    "name": str,
    "creator": str,
    "level_id": int,
    "level_id_known": bool,
    "attempt": int,
    "attempt_known": bool,
    "practice_mode": bool,
    "practice_mode_known": bool,
    "source": str,
    "identity_available": bool,
}

CONFIGURATION = {
    "tier": int,
    "active_tier": str,
    "spatial_culling_enabled": bool,
    "particle_throttling_enabled": bool,
    "trigger_indexing_enabled": bool,
    "matrix_caching_enabled": bool,
    "adaptive_governor_enabled": bool,
    "memory_arena_enabled": bool,
    "diagnostics_overlay_enabled": bool,
    "export_diagnostics_on_exit": bool,
    "target_fps": (int, float),
    "target_frame_time_ms": (int, float),
    "particle_density_scale": (int, float),
    "max_active_particles": int,
    "cull_offscreen_emitters": bool,
    "camera_padding_x": int,
    "camera_padding_y": int,
    "spatial_cell_size": int,
    "sort_triggers_by_position": bool,
    "trigger_lookahead_distance": (int, float),
    "override_applied": bool,
    "features_disabled_for_baseline": list,
}

TELEMETRY = {
    "samples": int,
    "total_window_ms": (int, float),
    "avg_fps": (int, float),
    "fps_from_window": (int, float),
    "instant_max_fps_from_min_frame": (int, float),
    "instant_min_fps_from_max_frame": (int, float),
    "avg_frame_time_ms": (int, float),
    "median_frame_time_ms": (int, float),
    "p95_frame_time_ms": (int, float),
    "p99_frame_time_ms": (int, float),
    "min_frame_time_ms": (int, float),
    "max_frame_time_ms": (int, float),
    "stddev_frame_time_ms": (int, float),
    "avg_update_cpu_time_ms": (int, float),
    "avg_update_thread_cpu_time_ms": (int, float),
    "avg_optigd_overhead_us": (int, float),
    "avg_culling_us": (int, float),
    "avg_particle_us": (int, float),
    "avg_trigger_us": (int, float),
    "avg_matrix_us": (int, float),
    "avg_governor_us": (int, float),
    "total_objects_culled": int,
    "total_particles_avoided": int,
    "total_triggers_skipped": int,
    "total_matrix_cache_hits": int,
    "total_matrix_cache_misses": int,
    "peak_arena_bytes": int,
    "total_arena_allocations": int,
    "total_heap_allocations_avoided": int,
    "peak_total_objects": int,
    "peak_active_particles": int,
    "sampling": dict,
}

SAMPLING = {
    "percentile_method": str,
    "sample_ring_capacity": int,
    "sample_ring_used": int,
    "sample_ring_overflow": bool,
    "overflow_samples": int,
    "histogram_saturated": bool,
    "histogram_max_ms": (int, float),
    "histogram_bin_width_ms": (int, float),
    "percentile_definition": str,
}

INTEGRITY = {
    "pause_events": int,
    "reset_events": int,
    "gap_events": int,
    "tier_transitions": int,
    "fallback_events": int,
    "excluded_samples": int,
    "excluded_gap_ms": (int, float),
    "paused_ms": (int, float),
    "config_override_applied": bool,
    "warnings": list,
}

# Definitions that must be present, because the whole point of the record is that
# a reader can tell internal instrumentation from an external presented-frame
# measurement without asking the author.
REQUIRED_DEFINITIONS = ("avg_fps", "captured_duration_ms", "wall_duration_ms",
                        "avg_update_cpu_time_ms", "avg_update_thread_cpu_time_ms",
                        "avg_optigd_overhead_us", "baseline_mode")

INDEX_TOP_LEVEL = {
    "index_kind": str,
    "schema_version": int,
    "optigd_version": str,
    "session_count": int,
    "note": str,
    "sessions": list,
}

INDEX_ENTRY = {
    "id": str,
    "file": str,
    "mode": str,
    "state": str,
    "started_utc": str,
    "level_name": str,
    "captured_duration_ms": (int, float),
    "avg_fps": (int, float),
    "samples": int,
    "comparable": bool,
    "readable": bool,
}


def check_section(data, schema, section, errors, path=""):
    if not isinstance(data, dict):
        errors.append("%s%s must be a JSON object" % (path, section))
        return
    for key, expected in schema.items():
        if key not in data:
            errors.append("%s%s.%s is missing" % (path, section, key))
            continue
        value = data[key]
        # bool is a subclass of int in Python; never accept True where a number
        # is documented.
        if expected is not bool and isinstance(value, bool):
            errors.append("%s%s.%s is a bool, expected %s"
                          % (path, section, key, _type_name(expected)))
            continue
        if not isinstance(value, expected):
            errors.append("%s%s.%s has type %s, expected %s"
                          % (path, section, key, type(value).__name__,
                             _type_name(expected)))


def _type_name(expected):
    if isinstance(expected, tuple):
        return " or ".join(getattr(t, "__name__", str(t)) for t in expected)
    return getattr(expected, "__name__", str(expected))


def _finite(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) \
        and math.isfinite(float(value))


def validate_session(doc, expect_simulated=False, label="") -> list:
    errors: list = []
    prefix = ("%s: " % label) if label else ""
    if not isinstance(doc, dict):
        return [prefix + "session root must be a JSON object"]

    check_section(doc, TOP_LEVEL, "root", errors)
    if errors:
        return [prefix + e for e in errors]

    if doc["report_kind"] != REPORT_KIND:
        errors.append("report_kind is '%s', expected '%s'"
                      % (doc["report_kind"], REPORT_KIND))
    if doc["schema_version"] != SCHEMA_VERSION:
        errors.append("schema_version is %s, expected %s"
                      % (doc["schema_version"], SCHEMA_VERSION))

    classification = doc["evidence_classification"]
    if classification not in KNOWN_CLASSIFICATIONS:
        errors.append("evidence_classification '%s' is not one of %s"
                      % (classification, ", ".join(KNOWN_CLASSIFICATIONS)))
    elif classification == INTERNAL_TELEMETRY:
        errors.append("a benchmark session must not be classified %s; that tag "
                      "belongs to the diagnostics report" % INTERNAL_TELEMETRY)
    if expect_simulated and classification != SIMULATED:
        errors.append("evidence_classification must be '%s' for a record emitted "
                      "without a Geometry Dash process (got '%s')"
                      % (SIMULATED, classification))
    if not doc["evidence_classification_note"].strip():
        errors.append("evidence_classification_note must explain the classification")

    check_section(doc["session"], SESSION, "session", errors)
    check_section(doc["platform"], PLATFORM, "platform", errors)
    check_section(doc["level"], LEVEL, "level", errors)
    check_section(doc["configuration"], CONFIGURATION, "configuration", errors)
    check_section(doc["telemetry"], TELEMETRY, "telemetry", errors)
    check_section(doc["integrity"], INTEGRITY, "integrity", errors)
    if isinstance(doc["telemetry"], dict):
        check_section(doc["telemetry"].get("sampling", {}), SAMPLING,
                      "telemetry.sampling", errors)
    for name in REQUIRED_DEFINITIONS:
        definition = doc["measurement_definitions"].get(name) if isinstance(
            doc["measurement_definitions"], dict) else None
        if not isinstance(definition, str) or not definition.strip():
            errors.append("measurement_definitions.%s is missing or empty" % name)
    if not doc["limitations"]:
        errors.append("limitations must not be empty: a session record always has "
                      "limits worth stating")
    elif not any("internal telemetry" in str(x).lower() for x in doc["limitations"]):
        errors.append("limitations must disclose that this is internal telemetry, "
                      "not an external presented-frame measurement")

    if errors:
        return [prefix + e for e in errors]

    errors.extend(_validate_session_semantics(doc, prefix))
    return errors


def _validate_session_semantics(doc, prefix="") -> list:
    """Cross-field checks: the record must agree with itself."""
    errors: list = []
    s = doc["session"]
    t = doc["telemetry"]
    i = doc["integrity"]
    c = doc["configuration"]
    sampling = t["sampling"]

    if s["mode"] not in MODES:
        errors.append("session.mode '%s' is not one of %s" % (s["mode"], ", ".join(MODES)))
    if s["state"] not in STATES:
        errors.append("session.state '%s' is not a terminal state; a persisted "
                      "session is never still running" % s["state"])
    if s["status"] != s["state"]:
        errors.append("session.status '%s' contradicts session.state '%s'"
                      % (s["status"], s["state"]))
    if not s["id"].strip():
        errors.append("session.id must not be empty")

    # --- frame budget: every offered frame is accounted for exactly once -------
    accounted = s["frames_counted"] + s["frames_excluded"] + s["frames_rejected"]
    if accounted != s["frames_observed"]:
        errors.append("frame budget does not add up: counted %d + excluded %d + "
                      "rejected %d = %d, but frames_observed is %d"
                      % (s["frames_counted"], s["frames_excluded"],
                         s["frames_rejected"], accounted, s["frames_observed"]))
    for key in ("frames_observed", "frames_counted", "frames_excluded", "frames_rejected"):
        if s[key] < 0:
            errors.append("session.%s is negative" % key)

    # --- the same counters appear in telemetry and integrity -------------------
    if t["samples"] != s["frames_counted"]:
        errors.append("telemetry.samples %d contradicts session.frames_counted %d"
                      % (t["samples"], s["frames_counted"]))
    if i["excluded_samples"] != s["frames_excluded"]:
        errors.append("integrity.excluded_samples %d contradicts "
                      "session.frames_excluded %d"
                      % (i["excluded_samples"], s["frames_excluded"]))
    if s["frames_rejected"] > 0 and "rejected_samples" not in i["warnings"]:
        errors.append("rejected samples must raise the 'rejected_samples' warning")

    # --- window ----------------------------------------------------------------
    if s["ended_epoch_ms"] < s["started_epoch_ms"]:
        errors.append("session ended before it started")
    epoch_span = s["ended_epoch_ms"] - s["started_epoch_ms"]
    if s["wall_duration_ms"] < 0:
        errors.append("session.wall_duration_ms is negative")
    elif epoch_span and abs(s["wall_duration_ms"] - epoch_span) > max(
            2000.0, s["wall_duration_ms"] * 0.05):
        errors.append("wall_duration_ms %.0f disagrees with the epoch span %d ms"
                      % (s["wall_duration_ms"], epoch_span))
    if s["captured_duration_ms"] > s["wall_duration_ms"] + 1.0:
        errors.append("captured_duration_ms %.1f exceeds wall_duration_ms %.1f: "
                      "active gameplay cannot outlast the elapsed wall clock"
                      % (s["captured_duration_ms"], s["wall_duration_ms"]))
    if abs(t["total_window_ms"] - s["captured_duration_ms"]) > 1.0:
        errors.append("telemetry.total_window_ms %.1f contradicts "
                      "session.captured_duration_ms %.1f"
                      % (t["total_window_ms"], s["captured_duration_ms"]))
    if i["paused_ms"] > i["excluded_gap_ms"] + 1.0:
        errors.append("integrity.paused_ms %.1f exceeds excluded_gap_ms %.1f: "
                      "paused time is a subset of excluded time"
                      % (i["paused_ms"], i["excluded_gap_ms"]))

    # --- state must follow from the data ---------------------------------------
    comparable_expected = (s["state"] == "completed" and t["samples"] > 0
                           and bool(s["id"].strip()))
    if s["comparable"] != comparable_expected:
        errors.append("session.comparable is %s but the data says %s"
                      % (s["comparable"], comparable_expected))
    if s["state"] == "completed" and t["samples"] == 0:
        errors.append("a completed session with zero samples is a contradiction")
    if s["state"] == "completed" and s["target_duration_ms"] > 0 and \
            s["captured_duration_ms"] < s["target_duration_ms"] * 0.98:
        errors.append("state 'completed' with only %.0f ms of a %.0f ms target"
                      % (s["captured_duration_ms"], s["target_duration_ms"]))
    if s["state"] != "completed" and not i["warnings"]:
        errors.append("a session that did not complete must say why in "
                      "integrity.warnings")
    if s["state"] == "cancelled" and "cancelled_by_user" not in i["warnings"]:
        errors.append("a cancelled session must carry 'cancelled_by_user'")

    # --- statistics -------------------------------------------------------------
    for key in ("avg_fps", "fps_from_window", "avg_frame_time_ms",
                "median_frame_time_ms", "p95_frame_time_ms", "p99_frame_time_ms",
                "min_frame_time_ms", "max_frame_time_ms", "stddev_frame_time_ms",
                "total_window_ms"):
        if not _finite(t[key]):
            errors.append("telemetry.%s is not a finite number (%r): a non-finite "
                          "value must be serialized as null, never as a number"
                          % (key, t[key]))
        elif t[key] < 0:
            errors.append("telemetry.%s is negative" % key)

    if t["samples"] == 0:
        if t["avg_fps"] != 0 or t["avg_frame_time_ms"] != 0:
            errors.append("telemetry reports fps/frame-time with zero samples")
    else:
        order = [("min_frame_time_ms", t["min_frame_time_ms"]),
                 ("median_frame_time_ms", t["median_frame_time_ms"]),
                 ("p95_frame_time_ms", t["p95_frame_time_ms"]),
                 ("p99_frame_time_ms", t["p99_frame_time_ms"]),
                 ("max_frame_time_ms", t["max_frame_time_ms"])]
        for (lower_name, lower), (upper_name, upper) in zip(order, order[1:]):
            if upper < lower - 1e-9:
                errors.append("telemetry.%s (%.4f) is below %s (%.4f): percentiles "
                              "are out of order" % (upper_name, upper, lower_name, lower))
        if t["avg_frame_time_ms"] > 0:
            implied = 1000.0 / t["avg_frame_time_ms"]
            if abs(t["avg_fps"] - implied) > max(0.5, implied * 0.02):
                errors.append("avg_fps %.3f is inconsistent with avg_frame_time_ms "
                              "%.3f (implies %.3f)"
                              % (t["avg_fps"], t["avg_frame_time_ms"], implied))
        mean = t["total_window_ms"] / t["samples"]
        if abs(mean - t["avg_frame_time_ms"]) > max(0.5, mean * 0.02):
            errors.append("avg_frame_time_ms %.3f is inconsistent with "
                          "total_window_ms / samples = %.3f"
                          % (t["avg_frame_time_ms"], mean))

    if sampling["percentile_method"] not in ("exact", "histogram_approximate"):
        errors.append("telemetry.sampling.percentile_method '%s' is not recognized"
                      % sampling["percentile_method"])
    if sampling["percentile_method"] == "histogram_approximate" and \
            "percentiles_approximated" not in i["warnings"]:
        errors.append("approximate percentiles must raise 'percentiles_approximated'")
    if sampling["sample_ring_overflow"] and "sample_ring_overflow" not in i["warnings"]:
        errors.append("a sample ring overflow must raise 'sample_ring_overflow'")
    if sampling["histogram_saturated"] and "histogram_saturated" not in i["warnings"]:
        errors.append("a saturated histogram must raise 'histogram_saturated'")
    if sampling["sample_ring_used"] > t["samples"]:
        errors.append("sample_ring_used %d exceeds the sample count %d"
                      % (sampling["sample_ring_used"], t["samples"]))
    if not sampling["percentile_definition"].strip():
        errors.append("the percentile definition must be stated in the record")

    # --- configuration actually in force ---------------------------------------
    if not c["override_applied"]:
        errors.append("configuration.override_applied must be true: a session is "
                      "only evidence if the capture's own configuration was "
                      "verified in force")
    if c["override_applied"] != i["config_override_applied"]:
        errors.append("configuration.override_applied contradicts "
                      "integrity.config_override_applied")
    if s["mode"] == "baseline":
        disabled = c["features_disabled_for_baseline"]
        if not disabled:
            errors.append("a baseline session must list the features it forced into "
                          "passthrough")
        for name in disabled:
            if not isinstance(name, str) or not name.strip():
                errors.append("features_disabled_for_baseline contains an empty name")
        if "spatial_culling" not in disabled or "particle_governor" not in disabled:
            errors.append("a baseline session must disclose that spatial culling and "
                          "the particle governor were in passthrough")
        for key, feature in (("spatial_culling_enabled", "spatial_culling"),
                             ("particle_throttling_enabled", "particle_governor"),
                             ("trigger_indexing_enabled", "trigger_indexer"),
                             ("matrix_caching_enabled", "matrix_cache"),
                             ("adaptive_governor_enabled", "adaptive_governor")):
            if c[key] and feature in disabled:
                errors.append("configuration.%s is enabled but '%s' is listed as "
                              "disabled for the baseline" % (key, feature))
        # Instrumentation stays on in a baseline: the disclosure, not a stripped mod.
        if not c["diagnostics_overlay_enabled"]:
            errors.append("a baseline session must keep the diagnostics overlay on: "
                          "the baseline is an internal control, not vanilla GD")
    else:
        if c["features_disabled_for_baseline"]:
            errors.append("an optimized session must not list baseline passthrough "
                          "features")

    # --- warnings are from the fixed vocabulary ---------------------------------
    known = {
        "level_reset", "level_exit", "cancelled_by_user", "paused", "long_gap",
        "tier_changed_during_capture", "safety_fallback_triggered",
        "config_override_reapplied", "capture_shorter_than_target",
        "no_samples_recorded", "sample_ring_overflow", "histogram_saturated",
        "percentiles_approximated", "level_identity_unavailable",
        "segment_not_contiguous", "wall_clock_exceeds_captured_window",
        "rejected_samples", "target_duration_mismatch", "persist_failed",
    }
    for w in i["warnings"]:
        if not isinstance(w, str) or w not in known:
            errors.append("integrity.warnings contains '%s', which is not a known "
                          "warning code" % w)
    if i["pause_events"] or i["paused_ms"] > 0:
        if "paused" not in i["warnings"]:
            errors.append("paused time must raise the 'paused' warning")
    if i["gap_events"] and "long_gap" not in i["warnings"]:
        errors.append("gap events must raise the 'long_gap' warning")
    if i["reset_events"] and not (
            "level_reset" in i["warnings"] or "segment_not_contiguous" in i["warnings"]):
        errors.append("level resets must be reported as an interruption or as a "
                      "non-contiguous segment")

    return [prefix + e for e in errors]


def validate_index(doc, session_ids=None, label="") -> list:
    errors: list = []
    prefix = ("%s: " % label) if label else ""
    if not isinstance(doc, dict):
        return [prefix + "index root must be a JSON object"]
    check_section(doc, INDEX_TOP_LEVEL, "index", errors)
    if errors:
        return [prefix + e for e in errors]
    if doc["index_kind"] != INDEX_KIND:
        errors.append("index_kind is '%s', expected '%s'" % (doc["index_kind"], INDEX_KIND))
    if "cache" not in doc["note"].lower():
        errors.append("the index must state that it is a rebuildable cache, not the "
                      "source of truth")

    entries = doc["sessions"]
    readable = [e for e in entries if isinstance(e, dict) and e.get("readable")]
    if doc["session_count"] != len(entries):
        errors.append("session_count %d does not match the %d listed sessions"
                      % (doc["session_count"], len(entries)))
    for n, entry in enumerate(entries):
        check_section(entry, INDEX_ENTRY, "sessions[%d]" % n, errors,
                      path="index.")
    if session_ids is not None:
        listed = {e.get("id") for e in readable}
        missing = session_ids - listed
        if missing:
            errors.append("the index does not list every readable session on disk: "
                          "%s" % ", ".join(sorted(missing)))
    return [prefix + e for e in errors]


def _load(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8")), None
    except json.JSONDecodeError as exc:
        return None, "%s is not valid JSON: %s" % (path, exc)
    except OSError as exc:
        return None, "%s could not be read: %s" % (path, exc)


def validate_path(path: Path, expect_simulated: bool) -> list:
    errors: list = []
    doc, error = _load(path)
    if error:
        return [error]
    if isinstance(doc, dict) and doc.get("index_kind") == INDEX_KIND:
        return validate_index(doc, label=path.name)
    return validate_session(doc, expect_simulated=expect_simulated, label=path.name)


def validate_directory(root: Path, expect_simulated: bool) -> tuple:
    """Returns (errors, session_count, modes_seen)."""
    errors: list = []
    json_files = sorted(p for p in root.rglob("*.json") if p.is_file())
    if not json_files:
        return (["%s contains no JSON files" % root], 0, set())

    session_ids: set = set()
    modes: set = set()
    sessions = 0
    index_docs = []
    for path in json_files:
        doc, error = _load(path)
        if error:
            errors.append(error)
            continue
        if not isinstance(doc, dict):
            errors.append("%s: root is not a JSON object" % path.name)
            continue
        if doc.get("index_kind") == INDEX_KIND:
            index_docs.append((path, doc))
            continue
        if doc.get("report_kind") != REPORT_KIND:
            errors.append("%s: unrecognized report_kind '%s'"
                          % (path.name, doc.get("report_kind")))
            continue
        sessions += 1
        errors.extend(validate_session(doc, expect_simulated=expect_simulated,
                                       label=str(path.relative_to(root))))
        session_id = doc.get("session", {}).get("id")
        if isinstance(session_id, str):
            if session_id in session_ids:
                # The store refuses to overwrite prior evidence, so two files
                # claiming one id means a copy escaped its directory.
                errors.append("session id '%s' appears in more than one file"
                              % session_id)
            session_ids.add(session_id)
        mode = doc.get("session", {}).get("mode")
        if isinstance(mode, str):
            modes.add(mode)

    for path, doc in index_docs:
        errors.extend(validate_index(doc, session_ids=session_ids,
                                     label=str(path.relative_to(root))))

    if sessions == 0:
        errors.append("%s contains no benchmark session records" % root)

    comparison = root / "comparison.txt"
    if sessions >= 2:
        # An artifact set with two sessions is an A/B pair; the comparison and its
        # provenance are part of the evidence, not an optional extra.
        if not comparison.exists():
            errors.append("comparison.txt is missing next to the emitted sessions")
        else:
            text = comparison.read_text(encoding="utf-8", errors="replace")
            if "--- provenance ---" not in text:
                errors.append("comparison.txt has no provenance section")
            if expect_simulated and SIMULATED not in text:
                errors.append("comparison.txt must state that the sessions are %s"
                              % SIMULATED)
            if not text.strip():
                errors.append("comparison.txt is empty")
        if "baseline" not in modes or "optimized" not in modes:
            errors.append("an A/B artifact set needs one baseline and one optimized "
                          "session (found: %s)" % ", ".join(sorted(modes)) or "none")
    return errors, sessions, modes


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    flags = {a for a in argv[1:] if a.startswith("--")}
    unknown = flags - {"--allow-in-game"}
    if len(args) != 1 or unknown:
        print(__doc__.strip().split("Usage:")[0])
        print("Usage: python3 tools/validate_benchmark_session_json.py "
              "<session.json|directory> [--allow-in-game]")
        return 2

    target = Path(args[0])
    if not target.exists():
        print("FAIL: not found: %s" % target)
        return 2

    # Records emitted by the standalone binaries have no game process behind them.
    # Validating a directory means validating such an artifact set unless the caller
    # says the records came from the real game.
    expect_simulated = "--allow-in-game" not in flags

    print("OptiGD benchmark session JSON schema validation")
    print("  target    : %s" % target)
    print("  expecting : %s" % (SIMULATED if expect_simulated
                                else "any known classification"))

    if target.is_dir():
        errors, sessions, modes = validate_directory(target, expect_simulated)
        print("  sessions  : %d (%s)" % (sessions, ", ".join(sorted(modes)) or "none"))
    else:
        errors = validate_path(target, expect_simulated)
        print("  sessions  : 1")

    for e in errors:
        print("  [ERROR] %s" % e)
    print("  result    : %s" % ("FAIL" if errors else "PASS"))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

# Gameplay Benchmark Protocol (v0.1.0-beta1)

Status: **BLOCKED.** No run under this protocol has been executed. Nothing in this repository
claims an FPS or frame-time improvement from OptiGD in Geometry Dash.

This document defines what a valid measurement is, before any result exists. It is a
pre-registration: the acceptance rules below are fixed, and results must not be reinterpreted
to fit them.

## 1. What must be measured

- **Claim being tested:** "With OptiGD v0.1.0-beta1 enabled, frame-time distribution on a fixed
  GD 2.2081 level and route is better than with OptiGD disabled."
- **Primary metric:** frame interval (time between consecutive presented frames), reported as
  mean FPS, p95 and p99 frame time, and count of frames over budget (> 16.67 ms at 60 Hz).
- **Secondary metric:** OptiGD overhead (`optigd_overhead_us` in the recorder CSV). This must be
  small relative to the frame budget.
- Synthetic microbenchmarks (`optigd_benchmark`) **never** satisfy this criterion.

## 2. Arms

| Arm | Mod state | Notes |
|---|---|---|
| Baseline | Geode 5.10.1 + GD 2.2081, OptiGD **not installed** | Same game build, settings, and device |
| Treatment | Same, plus OptiGD v0.1.0-beta1 `.geode` | Defaults only, unless a settings profile is named in the report |

The treatment must use the default settings from `mod.json` for the stated run. If a non-default
profile (for example `Custom`, or culling on) is measured, the report must name it and it must be
measured as its own arm. Culling is shadow-only in this release, so it is not expected to change
frame times at all. Measuring it is a check that the shadow pass is cheap.

## 3. Controlled conditions

- **Level:** one fixed, legally obtained level, chosen before testing and recorded by name, ID, and
  level-data hash. Use at least one level with > 30,000 objects. Do not commit level content.
- **Route:** a deterministic input replay (the same inputs at the same frame indices) or a
  scripted auto-play that is verified to produce the same death/completion frame in every run.
  Record the replay file's hash.
- **Device:** a named device with OS build, GPU driver version, power mode, and resolution. Desktop:
  AC power, no other heavy applications. Android: airplane mode, battery > 50 %, not charging,
  screen brightness fixed, thermal state recorded before every run.
- **Frame cap:** the same frame cap in both arms. Record it. Uncapped and vsync results must be
  reported separately.
- **Warm-up:** discard the first 60 frames after the level starts (see `--warmup-frames`).
- **Length:** at least 1,800 usable frames per run (30 s at 60 Hz).
- **Repetitions:** at least 5 runs per arm, interleaved (B, T, B, T, ...). Randomize which arm
  starts first. Record every run, including failed ones.
- **Cooling:** at least 5 minutes between runs on mobile devices.

## 4. Capture

The preferred measurement is external to the mod, so it does not depend on the code under test:

- **Windows:** PresentMon (or an equivalent ETW frame-time tracer), with the GD process selected.
  Export per-frame present times. Convert to the analyzer's inputs in a separate, recorded step.
- **Android:** `adb shell dumpsys gfxinfo <package> framestats` (reset between runs with
  `dumpsys gfxinfo <package> reset`), plus the thermal and battery state.
- **In-mod recorder (secondary):** setting `record-frametimes` writes
  `frametimes.csv` into the mod save directory. It records what OptiGD measured internally and
  can be compared with the external capture to check that the timing source is consistent.

Each run produces: the raw capture (kept **outside Git**), the recorder CSV (if used), the
replay hash, the build hashes of both `.geode` files, the device record, and the level hash.

## 5. Analysis

```bash
python3 tools/analyze_frametimes.py compare baseline_run01.csv treatment_run01.csv \
    --warmup-frames 60 --min-frames 1800
```

The tool reports per-run summaries and a 95 % bootstrap confidence interval for the difference in
mean frame interval. The protocol also requires, across runs:

- median of per-run mean FPS, per arm;
- median of per-run p95 frame time, per arm;
- total frames over budget, per arm.

Use the external capture for the headline numbers, and the recorder CSV only as a consistency
check. Do not pool runs with different devices, levels, routes, or builds.

## 6. Acceptance criteria (fixed in advance)

OptiGD v0.1.0-beta1 may be described as **improving** frame time on a given device, level, and
settings profile only if **all** of the following hold:

1. At least 5 valid runs per arm, with the same level hash, replay hash, and device.
2. The 95 % CI of the mean frame-interval difference (treatment minus baseline) is entirely
   below zero.
3. The median p95 frame time of the treatment is not higher than the baseline's.
4. The median count of frames over budget is not higher for the treatment.
5. The median `optigd_overhead_us` is below 5 % of the mean frame budget.
6. No crash, no soft-lock, and no change in the level's completion frame across runs.

If any criterion fails, the result is reported as **no measured improvement** for that scope. A
regression must be reported as a regression.

Anything not meeting these criteria is labelled `UNVERIFIED`, `PARTIAL`, or `DISABLED` in
`EVIDENCE_REPORT.md`.

## 7. Reporting labels

Every number in documentation must carry one of these labels:

- **[MEASURED]**: produced by a run under this protocol. Include the run ID and the raw-data
  location (not necessarily committed).
- **[SYNTHETIC]**: produced by a host-side microbenchmark or unit-level harness. Never presented
  as gameplay.
- **[PROJECTED]**: an estimate derived from a model, with the model stated.
- **[UNVERIFIED]**: not measured.

## 8. Current blockers

These must be resolved before the protocol can run:

1. **No Geometry Dash 2.2081 client** in this environment (proprietary; cannot be downloaded into the
   repository or the sandbox).
2. **No Geode runtime or test device** (Windows PC with GD; Android device with GD).
3. **No produced `.geode` artifacts** yet; the CI build for Win64, Android32, and Android64 must pass
   first.
4. **Level and replay** must be chosen and their hashes recorded by the person who owns the game
   install.

Until these are resolved the benchmark acceptance criterion stays `BLOCKED`, and the PR is a Draft.

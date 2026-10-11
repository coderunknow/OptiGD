# OptiGD `v0.1.0-beta.3` Benchmark & Telemetry Guide

## 1. Three kinds of number, kept apart

OptiGD produces three fundamentally different kinds of performance number. They
are never merged into one table, and every artifact states which one it is.

| Tag | What it measures | Where it comes from | Can it support a claim about how the game runs? |
|---|---|---|---|
| **`[MEASURED-IN-GAME]`** | Inter-frame intervals **inside the game thread**, plus OptiGD's own per-subsystem CPU cost and counters | Benchmark Mode session records (`benchmarks/<id>.json`), written by a live GD `2.2081` + Geode `v5.10.1` process | Partially. It is real in-game telemetry, but it is **internal instrumentation**: it cannot see GPU work, swap/present timing, or driver-level pacing. |
| **`[EXTERNAL]`** | Frames the GPU actually **presented** | CapFrameX CSV captures on Windows x64, compared with `tools/compare_capframex.py` | Yes — this is the only source that can. It cannot see any OptiGD internal. |
| **`[SIMULATED]`** | Algorithmic cost of OptiGD's data structures against naive `O(N)` scans | `optigd_benchmark` stress harness, and `optigd_tests --emit-sessions` (real session code, synthetic clock) | **No.** Not Geometry Dash frame times. No live-game gain is implied. |

Two further tags appear in the verification documents: **`[UNVERIFIED / BLOCKED]`**
for checks that need hardware this environment does not have, and **`[NOT RUN]`**
for checks that exist but did not execute. Both are stated rather than reported as
passes.

---

## 2. In-game Benchmark Mode `[MEASURED-IN-GAME]`

### 2.1 What it is

A capture workflow that runs inside Geometry Dash. It replaces the beta.2 protocol
of "edit six settings by hand, play, exit, copy a JSON file, run Python" with
explicit sessions that record what was actually in force.

It does **not** fabricate gameplay, force a level to complete, automate inputs, or
alter physics. Trigger execution stays native. Gameplay-critical objects
(obstacles, portals, pads, orbs, triggers) are never touched by decorative culling,
and a baseline capture keeps the diagnostics overlay and exporter on.

### 2.2 Reaching it

1. Install `coderunknow.optigd.geode`, launch GD `2.2081`, enter a level.
2. Press **Esc** (pause). The pause menu has an **OptiGD Benchmark** button.
3. The popup has six views: **Main**, **Results**, **Detail**, **Compare**,
   **Protocol**, **External**.

Buttons: `Baseline 60s`, `Optimized 60s`, `Cancel`, `Results`, `Compare`, `Detail`,
`Protocol`, `External`, `Export`, `As Baseline`, `As Optimized`, `Prev`, `Next`,
`Back`. While a capture runs, a compact HUD label in the level shows the state and
the time remaining. Nothing clickable is placed over the play area.

### 2.3 Mod settings

| Setting | Default | Effect |
|---|---|---|
| `benchmark-duration-seconds` | `60` (range 15–600) | Target **active gameplay** time per session. |
| `benchmark-stop-on-death` | `true` | On death/attempt restart: `true` finalizes immediately as `incomplete` with a `level_reset` interruption; `false` keeps measuring and records `reset_events` plus a `segment_not_contiguous` warning. |

### 2.4 What the 60 seconds counts

The window advances only by real inter-frame intervals while the game loop runs and
the level is actually being played:

- **Stalls** (interval ≥ `core::kStallThresholdMs`, 250 ms — pause, alt-tab, loader,
  scheduler pre-emption) are excluded from the window and counted as `gap_events`.
- **Paused frames** are excluded and accumulated in `paused_ms`. These are
  undetectable from the interval alone: with the pause menu open,
  `GJBaseGameLayer::update` still runs at the display rate, so every frame looks
  like ordinary gameplay. Only the explicit pause signal identifies them. Without
  that, a session could be "completed" without playing.
- **Non-finite or non-positive intervals** are rejected and counted, never averaged in.
- `wall_duration_ms` is recorded separately, so the ratio of active play to elapsed
  time is always visible.

The invariant `frames_observed == frames_counted + frames_excluded + frames_rejected`
holds for every session and is enforced out of process by
`tools/validate_benchmark_session_json.py`.

A session finalizes when accumulated active time reaches the target (`completed`),
when the user cancels (`cancelled`), on level exit or death (`incomplete`), or when
there is too little data to be a measurement at all (`invalid`). The state is
derived from the data, not from what the caller asked for: a capture with no samples
cannot be `completed`, and one shorter than `minValidCapturedMs` (500 ms) cannot be
`incomplete`. An explicit user cancellation is the one exception — it keeps saying
`cancelled`, because that is a fact the user knows, and it remains non-comparable.

### 2.5 Baseline is an internal control, not vanilla GD

`Baseline 60s` installs a **transient** configuration that forces spatial culling,
the particle governor, trigger indexing, the matrix cache and the adaptive governor
into passthrough. OptiGD remains installed; its hooks and instrumentation still run.

- The record's `mode_display` reads `BASELINE (OptiGD installed, optimizations in
  passthrough)`, and `measurement_definitions.baseline_mode` says the same in prose.
- `features_disabled_for_baseline` lists the CompatibilityLayer feature ids that
  were actually forced into passthrough, read back from the applied configuration —
  never from the setting labels, so it cannot claim a change that never reached the
  runtime.
- The transient override lives in a second `ConfigManager` layer. The user's saved
  settings are the base layer and are the only layer ever serialized, so a capture
  cannot corrupt them. The controller re-verifies the override every 16th frame and
  records `config_override_reapplied` if anything clobbered it.
- For an external vanilla baseline, see §3: that requires removing OptiGD entirely
  and measuring with CapFrameX.

### 2.6 Files

```
<GD save dir>/geode/mods/coderunknow.optigd/
├── benchmarks/
│   ├── 20261011T013512Z-baseline-0001.json     one file per session
│   ├── 20261011T014438Z-optimized-0002.json
│   ├── index.json                              rebuildable cache, never the source of truth
│   └── exports/                                Export button: session copies + comparison text
└── optigd_diagnostics.json                     the legacy per-level report (§5)
```

Ids sort chronologically as plain strings. Prior evidence is never overwritten: a
save is refused unless the bytes are identical (`errorCode == "already_exists"`),
writes are atomic (temp + rename), ids are validated against an allow-list so an id
cannot escape the directory, and nothing is ever pruned.

### 2.7 Comparing two sessions

The **Compare** view refuses rather than concludes. Blocking issues: no samples on
either side, a non-comparable state, both sides the same mode, platform or
architecture mismatch, level-identity mismatch, durations differing by more than
25 %, or overlapping capture windows. Non-blocking but surfaced: durations differing
by more than 10 %, and every integrity warning on either side.

Direction is judged per metric against a ±2 % noise threshold. Relative change from
a zero baseline is reported as **undefined with an explanatory note**, never as a
number. `significanceClaimed` is `false` by construction and the rendered text says
a single pair of captures cannot separate an optimization effect from run-to-run
variability.

**Protocol** (also shown in-game) is the repeated-run procedure: one level, one
segment, constant environment, restart the attempt before each capture, 3–5 pairs
with the order alternated, and compare the **spread** across pairs rather than the
best one.

### 2.8 Validating a session record

```bash
python3 tools/validate_benchmark_session_json.py <session.json>       # one record
python3 tools/validate_benchmark_session_json.py <directory>          # an artifact set
python3 tools/validate_benchmark_session_json.py <dir> --allow-in-game
```

A directory is treated as an artifact set emitted by the standalone binaries, so
every record must be `[SIMULATED]`; `--allow-in-game` is for records captured in the
real game. Beyond structure, the validator checks: the frame budget identity,
percentile ordering, `avg_fps` vs. mean frame time, `captured ≤ wall`, state vs.
data, the baseline passthrough disclosure, `override_applied`, the warning
vocabulary, index/session agreement, and that a session record's evidence
classification is one of the known tags.

Reproducible artifact set (real controller, synthetic clock, temporary directory —
**`[SIMULATED]`, not a measurement**):

```bash
./build/optigd_tests --emit-sessions /tmp/benchmark_sessions
python3 tools/validate_benchmark_session_json.py /tmp/benchmark_sessions
```

---

## 3. External verification with CapFrameX (Windows x64) `[EXTERNAL]`

### 3.1 Why this exists

OptiGD measures inter-frame intervals inside the game thread. CapFrameX measures
the frames the GPU actually presented. Only the second one can say whether
installing OptiGD helps a given machine. This procedure is also shown in-game under
**External**, and the text below is the same procedure.

**Status in this repository: `[NOT RUN]` / `[BLOCKED]`.** No Windows host, GPU or
CapFrameX binary was available where beta.3 was built, so no external capture
exists here. What is provided is the documented procedure, the comparison tool, and
a self-test of that tool. No external result is claimed or implied.

### 3.2 Procedure

**A. Capture WITHOUT OptiGD (external vanilla baseline)**
1. Move `coderunknow.optigd.geode` out of `<GD folder>\geode\mods` (or disable it in
   the Geode menu) and restart Geometry Dash.
2. Start CapFrameX, select the Geometry Dash process, and set a fixed hotkey for
   capture start/stop.
3. Enter the level, start the capture at 0 %, play exactly 60 s, stop the capture,
   exit the level.
4. Save the run as `vanilla_run1.csv` (CapFrameX: Analysis → Export → CSV).

**B. Capture WITH OptiGD**
5. Put the `.geode` back, restart GD, enable OptiGD, set the profile you intend to
   ship.
6. Repeat step 3 on the **same level and the same segment**.
7. Save as `optigd_run1.csv`.

**C. Keep conditions identical** — same resolution, window mode, display and refresh
rate; same GD FPS limiter / VSync setting (record what it was); same other mods
(ideally none); same background load; same capture length and start point.

**D. Repeat and compare** — at least 3 captures per side, alternating sides; compare
mean/median/P95/P99 frametime and the 1 % low FPS; compare the **distributions**,
not just the averages (two runs with the same mean can feel completely different).

**E. Keep the evidence together** — store the CSVs next to the exported OptiGD
session JSON, label which is which, and never merge the two into one table without
saying where each column came from.

### 3.3 Comparing the CSVs

```bash
python3 tools/compare_capframex.py \
    --label-a vanilla --label-b optigd \
    --a vanilla_run1.csv vanilla_run2.csv vanilla_run3.csv \
    --b optigd_run1.csv optigd_run2.csv optigd_run3.csv \
    --json external_comparison.json
```

The output is a per-capture table, a pooled side-by-side table, the run-to-run
spread, and an explicit statement that no statistical significance is claimed.
Percentiles use the same definition as OptiGD's internal statistics (P50 = median,
P95/P99 = nearest-rank `ceil(p*n)-1`), so an external table and an internal session
record can be read next to each other without a silent change of meaning.
`1% low FPS` is the average FPS of the worst 1 % of frames.

CSV reading is defensive because CapFrameX's export layout and delimiter have varied
between versions: the delimiter is detected (`,` `;` or tab), the frametime column is
matched by name, and an FPS-only export is converted. A file that cannot be
interpreted fails with a message naming the file and the columns found — numbers are
never guessed into existence. `--delimiter`, `--frametime-column` and `--fps-column`
override detection.

```bash
python3 tools/compare_capframex.py --self-test    # ctest: OptiGDExternalComparisonTool
```

The self-test's CSVs are **synthetic fixtures**. No CapFrameX CSV could be obtained
in the environment where the tool was written (network egress is restricted), so the
exact current column headers are matched heuristically rather than pinned to a
verified sample. That is stated here instead of being papered over.

### 3.4 What each side cannot see

| CapFrameX cannot see | OptiGD cannot see |
|---|---|
| OptiGD's per-subsystem CPU cost | Presented frames |
| Culling / particle / matrix counters | GPU queue time |
| The internal passthrough baseline | Driver-level pacing |

Report both, labelled. Android 32/64 has no practical equivalent of CapFrameX, so
Android results are internal telemetry only and must be reported as such.

---

## 4. Standalone synthetic stress benchmarks `[SIMULATED]`

### Reproducing locally
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPTIGD_STANDALONE_ONLY=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/optigd_benchmark benchmark_results.json
python3 tools/generate_report.py benchmark_results.json
```

### Measured results (Linux x86_64, GCC 12.2.0 `-O3`, seed `42`, median of 5 runs)

> Captured for `v0.1.0-beta.2` and **not re-run for beta.3** (beta.3 changed
> telemetry, configuration layering and percentiles, not the culling/particle/
> matrix hot paths these scenarios exercise). They are `[SIMULATED]` harness numbers
> on a shared 2-core container; absolute microsecond values move with host load, so
> the reproducible invariants are the **speedup factor** and the **zero per-frame
> heap allocations**, not the raw means.

| Scenario | Objects | Emitters | Triggers | Baseline Mean | OptiGD Mean | Baseline P99 | OptiGD P99 | Speedup | CPU Reduction |
|---|---|---|---|---|---|---|---|---|---|
| 1. Standard Rated Level (Medium Density) | 4,000 | 40 | 600 | 53.43 us | 9.58 us | 89.88 us | 49.22 us | **5.45x** | **81.64%** |
| 2. High-Object Extreme Demon (25k Objects) | 25,000 | 180 | 4,000 | 322.54 us | 60.07 us | 442.00 us | 201.79 us | **5.36x** | **81.34%** |
| 3. Ultra-Dense Mega-Collab Stress (60k Objects) | 60,000 | 350 | 10,000 | 724.13 us | 175.55 us | 995.48 us | 502.00 us | **4.19x** | **76.15%** |

| Scenario | Baseline Heap Allocations | OptiGD Per-Frame Heap Allocations | OptiGD Arena Bump Allocations |
|---|---|---|---|
| 1. Standard Rated Level (180 frames) | 1,785 | **0** | 360 |
| 2. High-Object Extreme Demon (180 frames) | 2,292 | **0** | 360 |
| 3. Ultra-Dense Mega-Collab Stress (120 frames) | 1,680 | **0** | 240 |

These isolate algorithmic complexity. **They are not Geometry Dash frame times and
imply no live-game frame-rate gain.**

### Artifact schema validation

Every JSON artifact is schema-checked in `ctest`, so a formatting regression cannot
silently ship a report downstream tooling cannot parse:

```bash
ctest --test-dir build --output-on-failure        # 6 checks
python3 tools/validate_benchmark_json.py         build/benchmark_results.json
python3 tools/validate_diagnostics_json.py       build/optigd_diagnostics.json
python3 tools/validate_benchmark_session_json.py build/benchmark_sessions
```

`tools/validate_benchmark_json.py` asserts `evidence_classification == "[SIMULATED]"`,
`benchmark_mode == "synthetic_stress_harness"` and `optigd_heap_allocations == 0` for
every scenario — a synthetic result cannot be laundered into a real-game
measurement, and a heap-allocation regression fails the build.

---

## 5. Legacy per-level diagnostics report `[MEASURED-IN-GAME]`

`optigd_diagnostics.json` predates Benchmark Mode and is still written on level exit
when `export-diagnostics-on-exit` is enabled. It reports the level-scoped window
(`report_schema_version: 2`, with `window_*` disclosure keys and `paused_frames`),
keeps every beta.2 key, escapes every interpolated string, writes atomically, and
sanitizes non-finite values to `0` with a `notes.non_finite_values_sanitized` flag.
A file without `report_schema_version` is v1 and still validates.

Use Benchmark Mode for A/B work: it records the configuration that was actually in
force, refuses to overwrite prior evidence, and gates comparisons. Use the
diagnostics report for a single level's internal profile.

```bash
python3 tools/generate_report.py --compare baseline_level.json optigd_level.json
```

`--compare` prints a side-by-side table of average/min/max FPS, mean/median/P95/P99
frame times, mean update CPU time, mean OptiGD overhead, and the culling/particle/
heap-allocation counters. It is a descriptive diff of two internal reports: it does
not test significance and it does not know whether the two runs were comparable.

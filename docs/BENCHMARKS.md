# OptiGD `v0.1.0-beta.2` Benchmark & Telemetry Guide

## 1. Evidence Classification

All performance metrics in OptiGD are strictly separated into two categories:
1. **`[SIMULATED]` Standalone Stress Benchmarks**: Automated, deterministic C++20 benchmarks (`optigd_benchmark`) comparing OptiGD's data structures against naive $O(N)$ linear scans and per-frame heap allocations.
2. **`[UNVERIFIED / BLOCKED]` Real-Game Geometry Dash Runtime Benchmarks**: In-game telemetry captured inside Geometry Dash `2.2081` with Geode `v5.10.1` using `DiagnosticsEngine` (`optigd_diagnostics.json`). Because headless CI/sandbox containers do not have a Geometry Dash client binary or GPU display server, real-game benchmark validation is currently **`[BLOCKED]`** pending hardware tester runs.

---

## 2. Standalone Synthetic Stress Benchmarks `[SIMULATED]`

### Reproducing Locally
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DOPTIGD_STANDALONE_ONLY=ON
cmake --build build --config Release --parallel
./build/optigd_benchmark benchmark_results.json
python3 tools/generate_report.py benchmark_results.json
```

### Measured Results (Linux x86_64, GCC 12.2.0 `-O3`, Seed `42`, median of 5 runs)

> Regenerated for `v0.1.0-beta.2` after the `VisibilityCuller` scratch-buffer and
> `ParticleGovernor` O(1)-totals rework. These are `[SIMULATED]` harness numbers
> on a shared 2-core CI container; absolute microsecond values move with host
> load, so the reproducible invariants are the **speedup factor** and the
> **zero per-frame heap allocations**, not the raw means.

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

#### Schema validation

Both JSON artefacts are schema-checked in CI so a formatting regression can
never silently ship a report that downstream tooling cannot parse:

```bash
ctest --test-dir build --output-on-failure          # runs all four checks
python3 tools/validate_benchmark_json.py   build/benchmark_results.json
python3 tools/validate_diagnostics_json.py build/optigd_diagnostics.json
```

`tools/validate_benchmark_json.py` additionally asserts that
`evidence_classification` is still `[SIMULATED]`, `benchmark_mode` is
`synthetic_stress_harness`, and that `optigd_heap_allocations` is `0` for every
scenario — i.e. a synthetic result can never be laundered into a real-game
measurement, and a heap-allocation regression fails the build.

---

## 3. Real-Game Geometry Dash Comparative Benchmark Protocol `[UNVERIFIED / BLOCKED]`

To validate OptiGD `v0.1.0-beta.2` on a Windows x64 PC or Android device running Geometry Dash `2.2081` + Geode `v5.10.1`:

### Step 1: Install the `.geode` Package
Copy `coderunknow.optigd.geode` into your Geode `mods/` directory and launch Geometry Dash `2.2081`.

### Step 2: Capture Baseline Telemetry (Passthrough Mode)
1. Open **Geode -> OptiGD Settings**.
2. Disable **Spatial Frustum Culling**, **Particle System Governor**, **Adaptive Frame-Time Governor**, and **Transform Matrix Caching**.
3. Ensure **Export Diagnostics Report** (`export-diagnostics-on-exit`) is **Enabled**.
4. Play a target benchmark level from 0% to a fixed percentage (or in Practice/Auto mode for a fixed 60-second run) and exit the level.
5. Copy `<GD Save Dir>/geode/mods/coderunknow.optigd/optigd_diagnostics.json` to `baseline_level.json`.

### Step 3: Capture OptiGD Telemetry
1. Open **Geode -> OptiGD Settings**.
2. Set **Performance Profile** to `LowEnd` (or `Balanced`) and enable **Spatial Frustum Culling**, **Particle System Governor**, **Adaptive Frame-Time Governor**, and **Transform Matrix Caching**.
3. Play the exact same level segment and exit the level.
4. Copy `<GD Save Dir>/geode/mods/coderunknow.optigd/optigd_diagnostics.json` to `optigd_level.json`.

### Step 4: Generate the Comparative Markdown Report
Run:
```bash
python3 tools/generate_report.py --compare baseline_level.json optigd_level.json
```
The script outputs a side-by-side table comparing:
- Average FPS and Min/Max FPS
- Mean, Median (`P50`), `P95`, and `P99` Frame Times (`ms`)
- Mean Update CPU Time (`ms`) and Mean OptiGD Overhead (`us`)
- Total Objects Culled, Particles Avoided, and Heap Allocations Avoided

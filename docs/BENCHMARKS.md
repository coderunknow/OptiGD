# OptiGD `v0.1.0-beta1` Benchmark & Telemetry Guide

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

### Measured Results (Linux x86_64, GCC 12.2.0 `-O3`, Seed `42`)

| Scenario | Objects | Emitters | Triggers | Baseline Mean | OptiGD Mean | Baseline P99 | OptiGD P99 | Speedup | CPU Reduction |
|---|---|---|---|---|---|---|---|---|---|
| 1. Standard Rated Level (Medium Density) | 4,000 | 40 | 600 | 75.33 us | 11.57 us | 132.89 us | 55.89 us | **6.51x** | **84.64%** |
| 2. High-Object Extreme Demon (25k Objects) | 25,000 | 180 | 4,000 | 323.26 us | 94.21 us | 438.11 us | 299.81 us | **3.43x** | **70.86%** |
| 3. Ultra-Dense Mega-Collab Stress (60k Objects) | 60,000 | 350 | 10,000 | 738.62 us | 312.94 us | 961.50 us | 921.49 us | **2.36x** | **57.63%** |

| Scenario | Baseline Heap Allocations | OptiGD Per-Frame Heap Allocations | OptiGD Arena Bump Allocations |
|---|---|---|---|
| 1. Standard Rated Level (180 frames) | 2,520 | **0** | 360 |
| 2. High-Object Extreme Demon (180 frames) | 2,700 | **0** | 360 |
| 3. Ultra-Dense Mega-Collab Stress (120 frames) | 1,920 | **0** | 240 |

---

## 3. Real-Game Geometry Dash Comparative Benchmark Protocol `[UNVERIFIED / BLOCKED]`

To validate OptiGD `v0.1.0-beta1` on a Windows x64 PC or Android device running Geometry Dash `2.2081` + Geode `v5.10.1`:

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

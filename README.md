# OptiGD `v0.1.0-beta.3` — Production Performance Engine for Geometry Dash

[![Build & Verify OptiGD](https://github.com/coderunknow/OptiGD/actions/workflows/build.yml/badge.svg)](https://github.com/coderunknow/OptiGD/actions/workflows/build.yml)
[![Version](https://img.shields.io/badge/Version-v0.1.0--beta.3-blue.svg)](mod.json)
[![Geode SDK](https://img.shields.io/badge/Geode-v5.10.1-orange.svg)](https://geode-sdk.org)
[![GD Version](https://img.shields.io/badge/Geometry_Dash-2.2081-green.svg)](https://store.steampowered.com/app/322170/Geometry_Dash/)

**OptiGD** is a C++20 performance optimization engine and [Geode SDK](https://geode-sdk.org/) mod for **Geometry Dash 2.2081** across **Windows x64 (`Win64`)**, **Android 64-bit (`arm64-v8a`)**, and **Android 32-bit (`armeabi-v7a`)**.

OptiGD targets the primary CPU and memory bottlenecks in high-object Geometry Dash levels—camera frustum visibility evaluation, off-screen particle simulation, affine transform recalculation, frame-time jitter, and per-frame temporary heap allocations—while preserving 100% of native player physics and collision determinism.

---

## Verification & Beta Status (`v0.1.0-beta.3`)

OptiGD `v0.1.0-beta.3` is a release candidate. Every optimization claim in this repository is labeled with its verification status (see [`EVIDENCE_REPORT.md`](EVIDENCE_REPORT.md) for the complete module-by-module audit):

- **`[VERIFIED]`**: Core C++20 engine (`MemoryArena`, `SpatialHash`, `VisibilityCuller`, `ParticleGovernor`, `TriggerOptimizer`, `MatrixCache`, `AdaptiveGovernor`, `FallbackManager`, `ConfigManager`, `CompatibilityLayer`, `DiagnosticsEngine`, `SampleWindow`, `JsonValue`, and the whole of Benchmark Mode) passes 100% of unit and integration tests (**50 test functions**, `ctest` **6/6**) and compiles into verified `.geode` binaries for **Windows x64**, **Android64**, and **Android32** using Geode SDK `v5.10.1`.
- **`[VERIFIED]`**: `v0.1.0-beta.3` satisfies Geode's version grammar (`v0.1.0-beta.3`, dot before the `3` — `v0.1.0-beta3` is rejected). The root cause of the earlier loader defect and its evidence chain are in [`EVIDENCE_REPORT.md`](EVIDENCE_REPORT.md#root-cause-analysis-rca).
- **`[SIMULATED]`**: Synthetic stress benchmarks (`optigd_benchmark`) demonstrate **4.19x – 5.45x** CPU reduction in standalone 4k–60k object simulation loops, with per-frame heap allocations driven to **0**. Those numbers were captured for beta.2 and are not re-run for beta.3; they are not Geometry Dash frame times.
- **`[MEASURED-IN-GAME]` — mechanism only**: Benchmark Mode is implemented end to end (in-game entry point, transient baseline/optimized configuration, session records, comparison, export) and its record format is validated out of process. The sessions in this repository were produced by the real controller with a **synthetic clock and no game process**, so they are stamped `[SIMULATED]`.
- **`[UNVERIFIED / BLOCKED]`**: Live in-game Geometry Dash `2.2081` captures on physical Windows/Android hardware, and the external CapFrameX procedure, remain **`[NOT RUN]`** — this environment has no GD binary, no GPU/display, no Geode loader, no Windows host and no Android device. **No claim of a live-game frame-rate gain is made anywhere in this repository, and the working benchmark system is not evidence of one.**

---

## Core Optimization Subsystems

1. **2D Spatial Hash & Differential Frustum Culling (`SpatialHash` + `VisibilityCuller`)**:
   - Indexes level objects in a flat 2D spatial hash grid (`128u` default cell size) with generational query stamps and incremental cell bucket re-indexing when objects move (`GJBaseGameLayer::updateObjectSection`).
   - Uses per-frame `MemoryArena` scratch buffers and differential visibility tracking (`evaluateVisibilityDelta`) so only decorative objects entering or leaving the padded camera frustum are toggled (`O(k)` visible objects instead of `O(N)` total level objects).
   - **Gameplay Safety**: Never hides non-decorative obstacles, portals, pads, orbs, or triggers, and respects GD's `m_isHide`, `m_isInvisible`, and `m_isDisabled` flags.

2. **Particle System Governor (`ParticleGovernor`)**:
   - Intercepts `cocos2d::CCParticleSystem::update(float dt)` during active gameplay.
   - Puts continuous off-screen particle emitters to sleep while keeping **`Critical`** emitters (player icon trails) and **finite-duration / auto-remove** emitters active so burst lifetimes always finish cleanly.
   - Stores each emitter's initial `baseEmissionRate` to apply non-compounding density scaling (`50%` in `LowEnd`, `75%` in `Balanced`, `100%` in `HighEnd`), and clamps `dt` while resetting `m_fEmitCounter` upon waking from sleep to prevent particle bursts.

3. **Affine Transform Matrix Cache (`MatrixCache`)**:
   - Caches `cocos2d::CCAffineTransform`-equivalent `3x2` matrices (`AffineMatrix`) keyed by node pointer and transform parameters (`posX`, `posY`, `rotation`, `scaleX`, `scaleY`, `anchor`).
   - Supports explicit dirty invalidation (`markDirty`), recursive parent-to-child dirty propagation (`setParent`), and pointer eviction (`evict`) when objects are removed from sections.

4. **Spatial & Group Trigger Indexer (`TriggerOptimizer`)**:
   - Indexes triggers deterministically by `(triggerX, spawnOrder, id)` and by group ID, supporting `TouchOrPosition`, `TouchOnly` (hitbox intersection), `Spawn`, and `Continuous` semantics, `isMultiTriggered` re-firing, group toggles (`GJBaseGameLayer::toggleGroup`), and backward movement / level reset cursor rewind.
   - **Safe Default**: Disabled by default (`enable-trigger-indexing = false`); when enabled in-game, runs non-destructive lookahead window inspection alongside native GD trigger execution.

5. **Adaptive Frame-Time Governor & Safety Fallback Manager (`AdaptiveGovernor` + `FallbackManager`)**:
   - Tracks a rolling 120-frame window of mean, median (`P50`), `P95`, `P99`, and standard deviation (`stdDevMs`) using `MemoryArena` scratch memory.
   - Dynamically adjusts performance tiers (`HighEnd <-> Balanced <-> LowEnd`) with a 120-frame hysteresis cooldown (or locks to user parameters when set to `Custom`).
   - Automatically triggers safety fallbacks on severe frame-time jitter or per-feature CPU budget overruns, and restores pre-fallback settings once frame times stabilize.

6. **Local Diagnostics HUD & Comparative Telemetry Exporter (`DiagnosticsEngine` + `DiagnosticsOverlay`)**:
   - Optional in-game HUD (`CCLabelBMFont`) and automatic JSON telemetry export (`optigd_diagnostics.json`) on level exit—100% local with zero network calls.
   - The overlay labels its numbers as internal telemetry and keeps wall-clock update time and thread CPU time apart, so a screenshot of it cannot be read as an external presented-frame measurement.

7. **In-Game Benchmark Mode (`src/benchmark/*` + `src/ui/BenchmarkPanel.*` + `src/geode/BenchmarkUI.*`)** — new in `v0.1.0-beta.3`:
   - **Entry point**: an **OptiGD Benchmark** button added in `PauseLayer::customSetup()`, opening a `geode::Popup` with Main / Results / Detail / Compare / Protocol / External views, plus a compact in-level HUD label showing state and time remaining. Nothing clickable overlaps gameplay input.
   - **Two explicit captures**: `Baseline 60s` and `Optimized 60s`. The baseline is an **internal control** — OptiGD stays installed and instrumenting, only the optimizations are forced into verified passthrough — and every record says so.
   - **Transient configuration that cannot corrupt saved settings**: a second `ConfigManager` layer holds the capture's override; the base layer is the only one ever serialized. The controller re-verifies the override every 16th frame and records `config_override_reapplied` if anything clobbered it.
   - **The window counts active gameplay, not elapsed time**: stalls (≥ 250 ms) and paused frames are excluded and reported (`gap_events`, `paused_ms`), wall-clock elapsed is recorded separately, and `frames_observed == frames_counted + frames_excluded + frames_rejected` always holds.
   - **Evidence, not conclusions**: sessions persist atomically under `<GD save dir>/geode/mods/coderunknow.optigd/benchmarks/` and prior results are never overwritten; comparisons block on incompatible sessions (different level, platform, duration, overlapping windows, non-comparable state) instead of manufacturing a result; relative change from a zero baseline is reported as undefined; and no statistical significance is ever claimed.
   - **No fabricated gameplay**: Benchmark Mode never automates input, forces completion, or alters physics. Trigger execution stays native, and gameplay-critical objects are never culled.
   - **100% local**: no telemetry upload, no network reporting, no analytics. See [`docs/BENCHMARKS.md`](docs/BENCHMARKS.md).

---

## Cross-Platform Geode Hook Architecture

All hooks in `src/geode/Hooks.cpp` are compiled unconditionally across **Windows x64**, **Android64**, and **Android32**:

| Hook Class | Target Method | Role |
|---|---|---|
| `GJBaseGameLayer` | `update(float dt)` | Drives `OptiGDManager::beginFrame()` / `endFrame()`, recording both wall-clock inter-frame time (`totalFrameTimeUs`) and update CPU time (`updateCpuTimeUs`). |
| `GJBaseGameLayer` | `updateObjectSection(GameObject*)` | Marks moved object dirty in `MatrixCache` and re-indexes its world bounding box in `VisibilityCuller` / `SpatialHash`. |
| `GJBaseGameLayer` | `removeObjectFromSection(GameObject*)` | Evicts removed object pointer from `MatrixCache`. |
| `GJBaseGameLayer` | `spawnGroup(...)` / `toggleGroup(...)` | Synchronizes group spawn and group enable/disable state with `TriggerOptimizer`. |
| `PlayLayer` | `init(...)` / `setupHasCompleted()` | Syncs mod settings, initializes level acceleration structures, and prepares sorted trigger indices. |
| `PlayLayer` | `addObject(GameObject*)` | Registers level objects, triggers (`EffectGameObject`), and attached particle emitters. |
| `PlayLayer` | `updateVisibility(float dt)` | Executes native `PlayLayer::updateVisibility(dt)`, then runs arena-backed differential decorative frustum culling when `enable-spatial-culling` is enabled. |
| `PlayLayer` | `postUpdate(float dt)` | Updates lookahead trigger telemetry and refreshes the in-game `DiagnosticsOverlay` HUD. |
| `PlayLayer` | `resetLevel()` / `onQuit()` | Rewinds trigger states on level reset, routes the interruption to a running benchmark session, and exports `optigd_diagnostics.json` on level exit. |
| `PauseLayer` | `customSetup()` | Adds the **OptiGD Benchmark** button that opens the Benchmark Mode popup. |
| `CCParticleSystem` | `update(float dt)` | Evaluates `ParticleGovernor` off-screen sleep/wake state and non-compounding emission scaling. |

Telemetry is gated on an active `PlayLayer`: `GJBaseGameLayer` is also the base of
`LevelEditorLayer`, and frames recorded while editing are not gameplay.

---

## Building & Running Tests Locally

### 1. Standalone C++20 Engine, Unit Tests & Synthetic Benchmarks
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPTIGD_STANDALONE_ONLY=ON
cmake --build build --config Release --parallel
ctest --test-dir build --output-on-failure --verbose      # 6 checks, 50 test functions
./build/optigd_benchmark benchmark_results.json
python3 tools/generate_report.py benchmark_results.json
```

Artifact validation, each of which is also a `ctest` check:
```bash
python3 tools/validate_geode_metadata.py mod.json
python3 tools/validate_diagnostics_json.py       build/optigd_diagnostics.json
python3 tools/validate_benchmark_json.py         build/benchmark_results.json
./build/optigd_tests --emit-sessions             build/benchmark_sessions
python3 tools/validate_benchmark_session_json.py build/benchmark_sessions
python3 tools/compare_capframex.py --self-test
```

### 2. Building the Geode Mod (`coderunknow.optigd.geode`)
With the [Geode CLI](https://docs.geode-sdk.org/) and `GEODE_SDK` (`v5.10.1`) installed:
```bash
geode build
```

### 3. Capturing & Comparing Real-Game Telemetry

In-game: pause → **OptiGD Benchmark** → `Baseline 60s`, restart the attempt,
`Optimized 60s`, then **Compare** / **Export**. Sessions land in
`<GD save dir>/geode/mods/coderunknow.optigd/benchmarks/`.

Externally (Windows x64, the only source that can see presented frames):
```bash
python3 tools/compare_capframex.py --label-a vanilla --label-b optigd \
    --a vanilla_run1.csv vanilla_run2.csv --b optigd_run1.csv optigd_run2.csv
```

Legacy per-level diagnostics reports:
```bash
python3 tools/generate_report.py --compare baseline_diagnostics.json optigd_diagnostics.json
```

The full protocol, the repeated-run requirements and the CapFrameX procedure are in
[`docs/BENCHMARKS.md`](docs/BENCHMARKS.md).

---

## Documentation Index

- [`AGENT.md`](AGENT.md) — Durable project governance, build/validation requirements, and anti-fabrication rules.
- [`DESIGN_LOG.md`](DESIGN_LOG.md) — Append-only engineering design log, audit findings, and verification record.
- [`EVIDENCE_REPORT.md`](EVIDENCE_REPORT.md) — Module-by-module verification table (`VERIFIED`, `PARTIAL`, `SIMULATED`, `UNVERIFIED`).
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — Detailed subsystem and Geode hook architecture.
- [`docs/BENCHMARKS.md`](docs/BENCHMARKS.md) — The three kinds of number (`[MEASURED-IN-GAME]`, `[EXTERNAL]`, `[SIMULATED]`), the in-game Benchmark Mode guide, the CapFrameX external procedure, and the synthetic harness results.
- [`docs/RELEASE_WORKFLOW.md`](docs/RELEASE_WORKFLOW.md) — Multi-platform CI/CD and release pipeline documentation.
- [`CHANGELOG.md`](CHANGELOG.md) — Release history for `v0.1.0-beta.3`.

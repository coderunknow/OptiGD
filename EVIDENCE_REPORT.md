# OptiGD `v0.1.0-beta1` Engineering Verification & Evidence Report

**Target Release**: `v0.1.0-beta1` (First Validated Beta Candidate)  
**Target Game / Loader**: Geometry Dash `2.2081` / Geode SDK `v5.10.1`  
**Target Platforms**: Windows x64 (`Win64`), Android 64-bit (`Android64` / `arm64-v8a`), Android 32-bit (`Android32` / `armeabi-v7a`)

---

## 1. Evidence Classification Taxonomy

To ensure complete transparency and zero fabricated claims, every subsystem and metric in OptiGD is classified using four strict tags:

- **`[VERIFIED]`**: Verified by automated unit/integration tests (`ctest`) and/or multi-platform Geode SDK compilation (`Win64`, `Android32`, `Android64`).
- **`[PARTIAL]`**: Core algorithm and unit tests are verified and hooks are wired safely, while live in-game Geometry Dash runtime validation remains pending.
- **`[SIMULATED]`**: Measured in the deterministic standalone C++20 stress benchmark suite (`optigd_benchmark`), not inside a live Geometry Dash `2.2081` process.
- **`[UNVERIFIED / BLOCKED]`**: Requires a runnable Geometry Dash `2.2081` + Geode `v5.10.1` graphical runtime environment, which is unavailable in headless CI/sandbox containers.

---

## 2. Module-by-Module Evidence Table

| Module | Status | Runtime Integration Point | Safe Default & Fallback Behavior | Automated Tests (`UnitTests.cpp`) | Platform Coverage | Supporting Evidence |
|---|---|---|---|---|---|---|
| **`MemoryArena`** (`src/core/MemoryArena.*`) | `VERIFIED` | `OptiGDManager::beginFrame()` resets arena; used per frame by `VisibilityCuller::evaluateVisibilityDelta()` (`SpatialHash::queryIDsArena`) and `AdaptiveGovernor::calculateWindowStats()`. | Power-of-two alignment normalization; 0-byte and overflow allocation guards return `nullptr`; auto-expands blocks on demand and reuses blocks across resets. | `testMemoryArena`: alignment (`1..64` + non-pow2), 0-byte guard, overflow rejection, block expansion & reuse. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + `AdaptiveGovernor` & `VisibilityCuller` arena integration. |
| **`SpatialHash`** (`src/core/SpatialHash.*`) | `VERIFIED` | Backing spatial index for `VisibilityCuller`; updated on `PlayLayer::addObject` and `GJBaseGameLayer::updateObjectSection`. | `Rect2D::normalized()` clamps non-finite/negative dimensions; `kMaxCellSpan = 64` caps pathological bounds; incremental cell re-indexing on `update()` and `remove()`. | `testSpatialHash`: insert, moving-object `update()` cell re-indexing, negative coordinates, `remove()`, and `queryArena()`. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass; verified zero stale bucket references after object movement. |
| **`OptiGDManager`** (`src/core/OptiGDManager.*`) | `VERIFIED` | Initialized in `$on_mod(Loaded)` with live `Loader::get()->getVersion().toVString()`; frame lifecycle driven by `GJBaseGameLayer::update(dt)`. | Separates `totalFrameTimeUs` (inter-frame wall/dt interval) from `updateCpuTimeUs` and `optigdOverheadUs`; propagates fallback/tier changes to `ConfigManager`. | `testManagerAndCompatibility`: full lifecycle, version check (`0.1.0-beta1`, `v5.10.1`), arena & avoided-heap accounting. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + cross-platform Geode hook compilation. |
| **`VisibilityCuller`** (`src/optimization/VisibilityCuller.*`) | `PARTIAL` | Hooked in `PlayLayer::addObject`, `PlayLayer::updateVisibility(dt)`, and `GJBaseGameLayer::updateObjectSection(GameObject*)`. | Runs *after* native `PlayLayer::updateVisibility(dt)`; only toggles visibility on decorative, non-trigger objects (`m_isDecoration \|\| m_isDecoration2`); respects `m_isHide`, `m_isInvisible`, `m_isDisabled`. | `testVisibilityCuller`: frustum culling, pointer-based bounds update, differential `evaluateVisibilityDelta` (`newlyVisible` / `newlyCulled`). | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + Geode `Win64`/`Android32`/`Android64` build; live GD visual check `[UNVERIFIED]`. |
| **`ParticleGovernor`** (`src/optimization/ParticleGovernor.*`) | `PARTIAL` | Hooked in `cocos2d::CCParticleSystem::update(float dt)` and `PlayLayer::addObject`. | Never sleeps `Critical` emitters (player trails) or finite-duration / `autoRemoveOnFinish` emitters; stores `baseEmissionRate` to prevent compounding decay; clamps `dt` and resets `m_fEmitCounter` on wake. | `testParticleGovernor`: critical priority preservation, finite-duration preservation, 50-frame non-compounding rate check, wake `dt` clamp. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + Geode `Win64`/`Android32`/`Android64` build; live GD visual check `[UNVERIFIED]`. |
| **`TriggerOptimizer`** (`src/optimization/TriggerOptimizer.*`) | `PARTIAL` | Hooked in `PlayLayer::addObject`, `PlayLayer::setupHasCompleted`, `PlayLayer::postUpdate`, `PlayLayer::resetLevel`, `GJBaseGameLayer::spawnGroup`, `GJBaseGameLayer::toggleGroup`. | **Disabled by default** (`enable-trigger-indexing = false`). When enabled in-game, runs non-destructive `inspectLookaheadWindow()` alongside native GD trigger execution; auto-disabled by `FallbackManager` if budget overrun occurs. | `testTriggerOptimizer`: `TouchOrPosition`, `TouchOnly` hitbox intersection, `isMultiTriggered`, deterministic `(x, spawnOrder, id)` sort, group toggle, backward cursor rewind. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + Geode `Win64`/`Android32`/`Android64` build. |
| **`MatrixCache`** (`src/optimization/MatrixCache.*`) | `PARTIAL` | Used in `extractGameObjectBounds` (`PlayLayer::addObject`, `GJBaseGameLayer::updateObjectSection`) and evicted in `GJBaseGameLayer::removeObjectFromSection`. | `markDirty()` invalidates node and recursively invalidates children; `getCached()` rejects dirty entries; `evict()` cleans up removed pointers; auto-disabled by `FallbackManager` if thrashing occurs. | `testMatrixCache`: cache hit/miss, `markDirty` invalidation, parent-to-child dirty propagation, pointer eviction, `AffineMatrix` equivalence. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + Geode `Win64`/`Android32`/`Android64` build. |
| **`AdaptiveGovernor`** (`src/adaptive/AdaptiveGovernor.*`) | `VERIFIED` | Executed at end of each frame in `OptiGDManager::endFrame()`. | Computes rolling 120-frame mean, median (P50), P95, P99, and std-dev using `MemoryArena` scratch memory; enforces 120-frame hysteresis cooldown; locked when tier is `Custom`. | `testAdaptiveGovernorAndFallback`: rolling window percentiles, automatic tier downgrade under budget pressure, arena usage. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass. |
| **`FallbackManager`** (`src/adaptive/FallbackManager.*`) | `VERIFIED` | Executed in `OptiGDManager::endFrame()` via `checkAndApplyFeatureFallbacks()`. | Detects high frame-time jitter (`stdDevMs > threshold`) or per-feature CPU overruns (`matrix_cache`, `trigger_indexer`), snapshots pre-fallback config, downgrades settings safely, and restores pre-fallback config after stable recovery frames. | `testAdaptiveGovernorAndFallback`: jitter fallback trigger, parameter downgrade, cooldown + stable frame recovery restoring pre-fallback config. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass. |
| **`Profiler`** (`src/profiling/Profiler.*`) | `VERIFIED` | Records per-frame & per-subsystem durations (`FrameUpdateCpu`, `OptiGDOverhead`, `VisibilityCuller`, `ParticleGovernor`, `TriggerOptimizer`, `AdaptiveGovernor`) in `OptiGDManager::endFrame()` and scoped timing via `OPTIGD_PROFILE_SCOPE`. | Thread-safe metric aggregation; reset on level load/shutdown; zero network dependencies. | `testManagerAndCompatibility`: verifies `FrameUpdateCpu` call count and `OPTIGD_PROFILE_SCOPE`. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + clean build across all targets. |
| **`DiagnosticsEngine`** (`src/diagnostics/DiagnosticsEngine.*`) | `VERIFIED` | Records per-frame stats in `OptiGDManager::endFrame()`; exports `optigd_diagnostics.json` on `PlayLayer::onQuit()`. | 100% local JSON telemetry export including `optigd_version`, `gd_version`, `geode_version`, P50/P95/P99 frame times, `avg_update_cpu_time_ms`, `avg_optigd_overhead_us`, and fallback history. | `testManagerAndCompatibility` + `tools/generate_report.py` single & `--compare` verification. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + CI `jq` schema validation. |
| **`DiagnosticsOverlay`** (`src/ui/DiagnosticsOverlay.*`) | `PARTIAL` | Rendered via `CCLabelBMFont` (`chatFont.fnt`) in `PlayLayer::init` and updated every 10 frames in `PlayLayer::postUpdate`. | Disabled by default (`enable-diagnostics-overlay = false`); displays `[OptiGD v0.1.0-beta1 Diagnostics]`, FPS, Mean/P50/P95/P99, Update CPU, OptiGD Overhead, Culling delta, Particles, Triggers, and Arena stats. | `testManagerAndCompatibility`: verifies formatted HUD output string and version header. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + Geode `Win64`/`Android32`/`Android64` build. |
| **`CompatibilityLayer`** (`src/compatibility/CompatibilityLayer.*`) | `VERIFIED` | Initialized in `OptiGDManager::initialize(gdVer, geodeVer)` and checked before executing hooks. | Validates GD `2.2081` and Geode `v5.10.1` compatibility; supports per-feature conflict isolation (`reportConflict`, `disableFeature`, `enableFeature`) without crashing the game. | `testManagerAndCompatibility`: verifies `2.2081` / `v5.10.1` version strings and conflict isolation/recovery. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass. |
| **`ConfigManager`** (`src/config/ConfigManager.*`) | `VERIFIED` | Synchronized in `Hooks.cpp` (`syncSettingsWithConfig()`) via `ConfigManager::syncFromSettings()`. | Applies selected tier preset (`LowEnd`, `Balanced`, `HighEnd`, `Custom`) first, then applies `Custom` parameters and feature flags without stale-copy overwrite; thread-safe serialization. | `testConfigAndPresetSync`: tests all 4 presets (`HighEnd` -> `Balanced` -> `LowEnd` -> `Custom`) and file/string round-trip serialization. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass. |

---

## 3. Synthetic Stress Benchmark Evidence `[SIMULATED]`

The following measurements were captured by executing the standalone C++20 benchmark suite (`./build/optigd_benchmark`) in this environment (Linux x86_64, GCC 12.2.0, `-O3`, fixed deterministic seed `42`):

| Scenario | Objects | Emitters | Triggers | Baseline Mean | OptiGD Mean | Baseline P99 | OptiGD P99 | Speedup | CPU Reduction |
|---|---|---|---|---|---|---|---|---|---|
| 1. Standard Rated Level (Medium Density) | 4,000 | 40 | 600 | 75.33 us | 11.57 us | 132.89 us | 55.89 us | **6.51x** | **84.64%** |
| 2. High-Object Extreme Demon (25k Objects) | 25,000 | 180 | 4,000 | 323.26 us | 94.21 us | 438.11 us | 299.81 us | **3.43x** | **70.86%** |
| 3. Ultra-Dense Mega-Collab Stress (60k Objects) | 60,000 | 350 | 10,000 | 738.62 us | 312.94 us | 961.50 us | 921.49 us | **2.36x** | **57.63%** |

*Note*: These `[SIMULATED]` benchmarks isolate the algorithmic complexity of `SpatialHash` differential frustum culling, `ParticleGovernor` offscreen sleeping, `TriggerOptimizer` lookahead indexing, `MatrixCache` lookup, and `MemoryArena` bump allocation against naive $O(N)$ linear scans.

---

## 4. Real-Game Geometry Dash Runtime Benchmarks `[UNVERIFIED / BLOCKED]`

### Status: `BLOCKED` by Headless Environment
- **Why Blocked**: Executing real-game benchmarks requires a licensed Geometry Dash `2.2081` binary, an active OpenGL/GLES GPU context, and the Geode `v5.10.1` runtime loader on Windows x64 or Android. None of these exist inside the headless Linux build/CI container.
- **Built-In Telemetry Harness for Hardware Testers**:
  OptiGD `v0.1.0-beta1` ships with a built-in comparative telemetry workflow:
  1. Install `coderunknow.optigd.geode` into Geometry Dash `2.2081` with Geode `v5.10.1`.
  2. **Baseline Capture**: In Mod Settings, disable `Spatial Frustum Culling`, `Particle System Governor`, `Adaptive Frame-Time Governor`, and `Transform Matrix Caching` (passthrough telemetry mode). Play a target level and exit; copy `<GD Save Dir>/geode/mods/coderunknow.optigd/optigd_diagnostics.json` to `baseline_level.json`.
  3. **OptiGD Capture**: Re-enable the optimization settings (`LowEnd` or `Balanced` preset), play the same level over the same segment, and copy `optigd_diagnostics.json` to `optigd_level.json`.
  4. **Generate Comparative Report**:
     ```bash
     python3 tools/generate_report.py --compare baseline_level.json optigd_level.json
     ```
     This outputs a Markdown comparison table with exact before/after deltas for Average FPS, Mean/Median(P50)/P95/P99 frame times, Mean Update CPU time, Culled Objects, Avoided Particles, and Avoided Heap Allocations.

---

## 5. Multi-Platform CI Build & Package Verification `[VERIFIED]`

Verified on GitHub Actions workflow **`Build & Verify OptiGD`** (Run ID `38045635961` on branch `arena/3343543c-optigd`):

| CI Job | Runner / Toolchain | Target | Verification Checks | Status |
|---|---|---|---|---|
| `Core Engine Tests & Benchmarks (ubuntu-latest)` (Job `114194480199`) | Ubuntu Linux x86_64 (GCC) | Standalone C++20 | `ctest` (9/9 suites), `optigd_benchmark`, `jq` version & `[SIMULATED]` checks, `tools/generate_report.py` | **PASSED (`[VERIFIED]`)** |
| `Core Engine Tests & Benchmarks (windows-latest)` (Job `114194480395`) | Windows Server x64 (MSVC) | Standalone C++20 | `ctest` (9/9 suites), `optigd_benchmark.exe`, `jq` version & `[SIMULATED]` checks, `tools/generate_report.py` | **PASSED (`[VERIFIED]`)** |
| `Geode Mod (Windows x64)` (Job `114194618511`) | Windows Server x64 (`clang++` MSVC ABI + Geode SDK `5.10.1`) | `Win64` | Compiled `Hooks.cpp` + `optigd_core`; verified `coderunknow.optigd.dll` and `mod.json` (`v0.1.0-beta1`) inside `.geode` archive | **PASSED (`[VERIFIED]`)** |
| `Geode Mod (Android64 (arm64-v8a))` (Job `114194618519`) | Ubuntu + Android NDK r29 (`aarch64-none-linux-android23` + Geode SDK `5.10.1`) | `Android64` | Compiled `Hooks.cpp` + `optigd_core`; verified `coderunknow.optigd.android64.so` and `mod.json` (`v0.1.0-beta1`) inside `.geode` archive | **PASSED (`[VERIFIED]`)** |
| `Geode Mod (Android32 (armeabi-v7a))` (Job `114194618537`) | Ubuntu + Android NDK r29 (`armv7a-linux-androideabi23` + Geode SDK `5.10.1`) | `Android32` | Compiled `Hooks.cpp` + `optigd_core`; verified `coderunknow.optigd.android32.so` and `mod.json` (`v0.1.0-beta1`) inside `.geode` archive | **PASSED (`[VERIFIED]`)** |
| `Package Combined .geode Artifact` (Job `114195085038`) | Ubuntu Linux + `geode-sdk/build-geode-mod/combine` | Multi-platform `.geode` | Verified combined `coderunknow.optigd.geode` (`447,492` bytes) contains `coderunknow.optigd.dll`, `coderunknow.optigd.android64.so`, `coderunknow.optigd.android32.so`, and `mod.json` (`v0.1.0-beta1`) | **PASSED (`[VERIFIED]`)** |


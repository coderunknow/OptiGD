# OptiGD Engineering Design Log

> Append-only record of task requirements, repository audit findings, architectural decisions, implementation progress, verification results, and unresolved blockers.

---

## Entry 1 — 2026-10-10: Task Initialization & Baseline Repository Audit

### 1.1 Task Objective & Fixed User Decisions
- **Goal**: Bring OptiGD to its first properly validated beta, `v0.1.0-beta1`, turning the documented optimization architecture into a genuinely integrated, testable, and measurable Geometry Dash 2.2081 mod for Geode `5.10.1`.
- **Fixed Decisions**:
  - **Scope**: Full set of documented optimization modules (`MemoryArena`, `SpatialHash`, `VisibilityCuller`, `ParticleGovernor`, `TriggerOptimizer`, `MatrixCache`, `AdaptiveGovernor`, `FallbackManager`, `Profiler`, `DiagnosticsEngine`, `DiagnosticsOverlay`, `CompatibilityLayer`, `ConfigManager`, and Geode gameplay hooks).
  - **Optimization Strategy**: Aggressive optimization is acceptable where guarded by configuration, correctness safeguards, fallback behavior, and tests.
  - **Build Targets**: Windows x64 (`Win64`), Android 32-bit (`Android32`), and Android 64-bit (`Android64`).
  - **Version**: `v0.1.0-beta1` (preserving Git history).
  - **Delivery**: Dedicated branch (`arena/3343543c-optigd`) and Pull Request against `main`. Do not merge, tag, or publish a GitHub Release.
  - **Performance Validation**: Real, reproducible Geometry Dash gameplay benchmarks against a controlled baseline are mandatory to claim beta performance gains. If no Geometry Dash client/runtime is available in the environment, prepare all in-game instrumentation and reproducible comparison tooling, leave the acceptance criterion explicitly blocked, and open a **Draft PR**.

### 1.2 Audit of Current Repository State (`7ecfdd1`)
Before modifying any code, all 54 files on `main` (`7ecfdd1c106a0a8e1e4c83a75fa2660a598fd4b4`) and the Geode v5.10.1 / GD 2.2081 bindings were inspected. All 11 investigation leads were verified against the source:

1. **Premature Version Metadata (`v1.0.0` vs `v0.1.0-beta1`)**:
   - Confirmed: `mod.json`, `CMakeLists.txt`, `README.md`, `CHANGELOG.md`, `EVIDENCE_REPORT.md`, `docs/ARCHITECTURE.md`, `src/core/OptiGDManager.cpp`, `src/diagnostics/DiagnosticsEngine.cpp`, `src/ui/DiagnosticsOverlay.cpp`, `src/geode/Hooks.cpp`, `src/benchmark/BenchmarkSuite.cpp`, and `tests/UnitTests.cpp` all declare `v1.0.0` / `1.0.0`.
2. **No-Op Spatial Culling Hook in `src/geode/Hooks.cpp`**:
   - Confirmed: `OptiPlayLayer::updateVisibility(float dt)` checks `if (!config.flags.enableSpatialCulling) { PlayLayer::updateVisibility(dt); return; }` and then immediately calls `PlayLayer::updateVisibility(dt);` in the enabled branch as well. `VisibilityCuller` and `SpatialHash` are never populated or queried by `Hooks.cpp`.
3. **Unconnected Subsystems & Missing Hooks**:
   - Confirmed: Despite `README.md` and `EVIDENCE_REPORT.md` claiming hooks for `PlayLayer`, `CCNode`, and `CCParticleSystemQuad`, `src/geode/Hooks.cpp` only hooks `PlayLayer` and `MenuLayer`. `ParticleGovernor`, `TriggerOptimizer`, `MatrixCache`, and `DiagnosticsOverlay` (in-game HUD label) have zero calls in `src/geode/Hooks.cpp`.
4. **`SpatialHash::update()` Fails to Re-Index Moving Objects (`src/core/SpatialHash.cpp`)**:
   - Confirmed: `SpatialHash::update(uint32_t id, const Rect2D& newBounds)` only writes `m_items[id].bounds = newBounds;` without updating `m_buckets`. Moving an object into a different cell causes spatial queries at the new position to miss the object completely, while leaving stale references in the old cell buckets. There is also no `remove()` method for destroyed objects.
5. **Simplified Trigger Semantics in `src/optimization/TriggerOptimizer.cpp`**:
   - Confirmed: `TriggerOptimizer::evaluate(float playerX, ...)` fires all `TouchOrPosition` triggers with `playerX >= trig->triggerX` as one-shot triggers (`isCompleted = true`). It does not distinguish touch-activated triggers (which require player hitbox collision) from X-position triggers, ignores multi-trigger (`m_isMultiTriggered`) re-activation, cannot rewind `m_cursorIndex` when moving backward in platformer mode or reverse portals or checkpoints, and ignores group toggle states. Furthermore, Geode's `GeometryDash.bro` notes `// this is probably pretty wrong :D` on `EffectGameObject` member offsets, meaning replacing GD's native `GJBaseGameLayer::checkSpawnObjects()` would break level compatibility.
6. **Geode Version Mismatch (`5.10.1` vs `v4.0.0`)**:
   - Confirmed: `mod.json` targets `"geode": "5.10.1"`, whereas `OptiGDManager::initialize()` and `CompatibilityLayer.hpp` hardcode `"v4.0.0"` and never query `geode::Loader::get()->getVersion()` or `getGameVersion()`.
7. **Simulated Benchmarks vs Unverified Real Gameplay**:
   - Confirmed: All 5 benchmark scenarios in `BenchmarkSuite.cpp` are synthetic CPU loops (`[SIMULATED]`). No Geometry Dash binary, display server, or Android device is present in this Linux container (`Debian 12 x86_64`).
8. **Android Gameplay Hooks Compiled Out (`#if defined(GEODE_IS_WINDOWS)`)**:
   - Confirmed: `class $modify(OptiPlayLayer, PlayLayer)` in `src/geode/Hooks.cpp` is wrapped in `#if defined(GEODE_IS_WINDOWS)`. On Android32 and Android64, only `OptiMenuLayer` is compiled—zero gameplay hooks run on Android. Inspection of `GeometryDash.bro` and `Cocos2d.bro` for GD 2.2081 confirms `PlayLayer`, `GJBaseGameLayer`, `GameObject`, `CCNode`, `CCParticleSystem`, and `CCParticleSystemQuad` all carry `[[link(android)]]` and export the required symbols on Android32 and Android64.
9. **`syncSettingsWithConfig()` Overwrites Tier Preset with Stale Copy**:
   - Confirmed: `syncSettingsWithConfig()` in `src/geode/Hooks.cpp` copies `auto config = configMgr.getConfig();` *before* calling `configMgr.applyTierPreset(...)`, and then calls `configMgr.setConfig(config);` at the end—completely overwriting the preset changes with the pre-preset copy. Additionally, `"Custom"` is not handled in `syncSettingsWithConfig()`, and `OptiGDManager::endFrame()` modifies a local copy of `config` in `AdaptiveGovernor::update(config)` and `FallbackManager::checkAndApplyFallbacks(..., config)` without ever writing the updated config back to `ConfigManager` or applying it to `VisibilityCuller` / `ParticleGovernor`.
10. **Misleading `heapAllocationsAvoided` Counter & Per-Frame Heap Allocations**:
    - Confirmed: `OptiGDManager::endFrame()` sets `m_currentStats.heapAllocationsAvoided = m_frameArena.getAllocationCount()`, but no runtime subsystem ever allocates from `m_frameArena`. Worse, `AdaptiveGovernor::calculateWindowStats()` allocates a `std::vector<double>` on the heap every single frame to compute P95/P99 percentiles.
11. **CI Workflow SDK Selection & Artifact Verification**:
    - Confirmed: `.github/workflows/build.yml` and `release.yml` use `sdk: latest` instead of `sdk: given` (which honors `mod.json`'s `"geode": "5.10.1"`), and `build.yml` does not verify per-target `.geode` contents or binary presence.
12. **Additional Bugs Found During Deep Code Inspection**:
    - `MatrixCache::markDirty(void* nodePtr)` increments `dirtyGeneration`, but `MatrixCache::getCached(...)` never checks `dirtyGeneration` or any dirty flag—returning stale matrices even after `markDirty()` is called.
    - `OptiPlayLayer::postUpdate(float dt)` measures only the execution time of `PlayLayer::postUpdate(dt)` between `beginFrame` and `endFrame` and records it as `totalFrameTimeUs`, causing `AdaptiveGovernor` and `DiagnosticsEngine` to treat sub-millisecond `postUpdate` CPU time as total frame interval (reporting thousands of FPS instead of actual frame pacing).
    - `DiagnosticsSummary::totalParticlesAvoided` is declared in `DiagnosticsEngine.hpp` but never populated in `DiagnosticsEngine.cpp`.

---

## Entry 2 — 2026-10-10: Implementation of `v0.1.0-beta1` Architecture, Fixes & Verification

### 2.1 Core & Optimization Subsystem Changes
1. **Version & Target Alignment (`src/core/Types.hpp`, `mod.json`, `CMakeLists.txt`)**:
   - Defined `kOptiGDVersion = "0.1.0-beta1"`, `kOptiGDVersionTag = "v0.1.0-beta1"`, `kTargetGDVersion = "2.2081"`, and `kTargetGeodeVersion = "v5.10.1"`.
   - Updated `mod.json` to `"version": "v0.1.0-beta1"` and `"geode": "5.10.1"`, and added user-configurable `Custom` profile settings (`custom-particle-density`, `custom-max-particles`, `custom-cull-offscreen-particles`, `custom-camera-padding-x`, `custom-camera-padding-y`, `target-fps`).
   - Updated `CMakeLists.txt` to `project(OptiGD VERSION 0.1.0 LANGUAGES CXX)` with `OPTIGD_VERSION_STRING = "0.1.0-beta1"`.
2. **`MemoryArena` (`src/core/MemoryArena.hpp/.cpp`)**:
   - Added power-of-two alignment normalization (`normalizeAlignment`), zero-byte allocation guard (`allocate(0) -> nullptr`), integer overflow guards, peak memory tracking (`getPeakUsage()`), and block reuse across `reset()`.
3. **`SpatialHash` (`src/core/SpatialHash.hpp/.cpp`)**:
   - Stored covered cell coordinates (`minCellX..maxCellX`, `minCellY..maxCellY`) on each `SpatialItem`.
   - Fixed `SpatialHash::update()` to diff old and new cell ranges, removing the item from cells it left and inserting it into cells it entered.
   - Added `SpatialHash::remove()` and zero-heap-allocation `queryArena()` / `queryIDsArena()` backed by `MemoryArena`.
4. **`VisibilityCuller` (`src/optimization/VisibilityCuller.hpp/.cpp`)**:
   - Added pointer-to-index lookup (`m_ptrToIndex`), `updateObjectBoundsByPtr()`, `unregisterObject()`, and `unregisterObjectByPtr()`.
   - Added arena-backed differential frustum evaluation (`evaluateVisibilityDelta()`), computing `newlyVisible` and `newlyCulled` decorative objects in $O(k)$ visible objects instead of scanning all $N$ objects.
5. **`ParticleGovernor` (`src/optimization/ParticleGovernor.hpp/.cpp`)**:
   - Stored `baseEmissionRate` per emitter so `effectiveEmissionRate = baseEmissionRate * multiplier` never compounds downward across frames.
   - Preserved `Critical` emitters (player trails) and finite-duration / `autoRemoveOnFinish` emitters offscreen so burst timers complete cleanly.
   - Added sleep/wake transition tracking (`justEnteredSleep`, `justWokeFromSleep`) with `dt <= 1/60s` clamping on wake.
6. **`TriggerOptimizer` (`src/optimization/TriggerOptimizer.hpp/.cpp`)**:
   - Added `TouchOnly` hitbox-intersection evaluation (`evaluateWithHitbox`), `isMultiTriggered` re-arming, deterministic `(triggerX, spawnOrder, id)` sorting, group toggle state (`setGroupEnabled`), backward movement cursor rewind (`rewindCursorTo`), and level reset state restoration (`resetTriggerStates`).
7. **`MatrixCache` (`src/optimization/MatrixCache.hpp/.cpp`)**:
   - Fixed `getCached()` to reject entries marked `dirty` or whose parent is dirty.
   - Added recursive parent-to-child dirty propagation (`setParent`, `markDirty`), pointer eviction (`evict`), and analytical Cocos2d-x `AffineMatrix::fromNodeParams` and `transformRect`.
8. **`AdaptiveGovernor`, `FallbackManager`, `ConfigManager`, & `OptiGDManager`**:
   - Connected `AdaptiveGovernor::calculateWindowStats()` to `MemoryArena` to eliminate per-frame heap allocations when sorting frame times, and added median (`P50`) calculation.
   - Added per-feature CPU overrun fallbacks (`checkAndApplyFeatureFallbacks`) and automatic pre-fallback configuration recovery to `FallbackManager`.
   - Fixed preset synchronization via `ConfigManager::syncFromSettings()`, ensuring `LowEnd`, `Balanced`, `HighEnd`, and `Custom` presets are applied cleanly without stale-copy overwrites.
   - Separated `totalFrameTimeUs` (inter-frame interval) from `updateCpuTimeUs` and `optigdOverheadUs`, and separated `arenaAllocations` from `heapAllocationsAvoided`.
9. **Cross-Platform Geode Integration (`src/geode/Hooks.cpp`)**:
   - Removed the `#ifdef GEODE_IS_WINDOWS` guard on gameplay hooks so `OptiGDBaseGameLayer` (`GJBaseGameLayer`), `OptiGDPlayLayer` (`PlayLayer`), and `OptiGDParticleSystem` (`CCParticleSystem`) compile and run on **Windows x64**, **Android64**, and **Android32**.
   - Passed live `Loader::get()->getVersion().toVString()` and `getGameVersion()` to `OptiGDManager::initialize()`.

### 2.2 Local Verification Results
- **Standalone Build (`Release`, `-Wall -Wextra -Wpedantic -O3`)**: Built cleanly with zero warnings.
- **Unit & Integration Test Suite (`ctest --output-on-failure --verbose`)**: 9/9 test suites passed (`testMemoryArena`, `testSpatialHash`, `testVisibilityCuller`, `testParticleGovernor`, `testTriggerOptimizer`, `testMatrixCache`, `testAdaptiveGovernorAndFallback`, `testConfigAndPresetSync`, `testManagerAndCompatibility`).
- **Synthetic Stress Benchmarks (`optigd_benchmark`)**: Verified `[SIMULATED]` speedups of **6.51x** (4k objects), **3.43x** (25k objects), and **2.36x** (60k objects) with 0 per-frame heap allocations.
- **Comparative Telemetry Tooling (`tools/generate_report.py`)**: Verified both single-report and `--compare <baseline.json> <optigd.json>` modes.

### 2.3 Unresolved Work & Unverified Claims
- **Real-Game Geometry Dash Runtime Benchmarks (`[UNVERIFIED / BLOCKED]`)**:
  - **Blocker**: This Linux build environment does not contain a runnable Geometry Dash `2.2081` client binary, Steam/Wine session, GPU display server, or Android device/emulator.
  - **Mitigation**: All in-game telemetry hooks (`DiagnosticsEngine` JSON export on level exit, passthrough baseline mode, and `tools/generate_report.py --compare`) are built into the `.geode` package, and the PR will be opened as a **Draft PR** explicitly documenting the blocked runtime benchmark acceptance criterion.


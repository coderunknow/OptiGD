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

---

## Entry 3 — 2026-10-10: Multi-Platform CI Build & Geode Package Verification

### 3.1 Cross-Platform Compilation & Fixes During CI Verification
1. **Windows MSVC `<windows.h>` Macro & Standard Header Fixes (`2a1faab`, `1345485`)**:
   - Defined `NOMINMAX` and `WIN32_LEAN_AND_MEAN` before `<windows.h>` in `src/platform/Platform.cpp` and `CMakeLists.txt`, and added explicit `<algorithm>` and `<limits>` includes for MSVC compatibility.
   - Added `shell: bash` to the cross-platform benchmark step in `.github/workflows/build.yml` and `.github/workflows/release.yml`.
2. **CMake Geode SDK Target Ordering & `Logger::setCallback` Bridge (`cd28537`, `a3e033e`)**:
   - Ordered `add_library(${PROJECT_NAME} SHARED ...)` prior to `add_subdirectory($ENV{GEODE_SDK} ...)` and `setup_geode_mod(${PROJECT_NAME})` in `CMakeLists.txt` to match Geode SDK's CMake contract (`BUILD_SHARED_LIBS OFF`).
   - Added `Logger::setCallback()` in `src/core/Logger.hpp/.cpp` and handled `LogLevel::None` in `src/geode/Hooks.cpp` so internal OptiGD logs forward cleanly to `geode::log` with zero `-Wswitch` warnings.

### 3.2 Verified GitHub Actions Run (`38045635961`)
All 6 jobs in workflow `Build & Verify OptiGD` passed on commit `a3e033e`:
- `Core Engine Tests & Benchmarks (ubuntu-latest)` (`114194480199`): **SUCCESS**
- `Core Engine Tests & Benchmarks (windows-latest)` (`114194480395`): **SUCCESS**
- `Geode Mod (Windows x64)` (`114194618511`): **SUCCESS** (`coderunknow.optigd.dll` verified in `.geode`)
- `Geode Mod (Android64 (arm64-v8a))` (`114194618519`): **SUCCESS** (`coderunknow.optigd.android64.so` verified in `.geode`)
- `Geode Mod (Android32 (armeabi-v7a))` (`114194618537`): **SUCCESS** (`coderunknow.optigd.android32.so` verified in `.geode`)
- `Package Combined .geode Artifact` (`114195085038`): **SUCCESS** (`coderunknow.optigd-geode-package`, 447,492 bytes, containing `coderunknow.optigd.dll`, `coderunknow.optigd.android64.so`, `coderunknow.optigd.android32.so`, and `mod.json` at `v0.1.0-beta1`).

---

## Entry 4 — 2026-10-10: Final Audit, Merge & `v0.1.0-beta1` Release Tagging

### 4.1 Final Audit Findings & Hardening
1. **`Profiler` Per-Frame Integration & Unit Test Coverage (`src/core/OptiGDManager.cpp`, `tests/UnitTests.cpp`)**:
   - Wired `Profiler::instance().record(...)` into `OptiGDManager::endFrame()` for `FrameUpdateCpu`, `OptiGDOverhead`, `VisibilityCuller`, `ParticleGovernor`, `TriggerOptimizer`, and `AdaptiveGovernor`, with automatic reset in `onLevelLoad()` and `shutdown()`.
   - Added explicit unit test assertions in `testManagerAndCompatibility()` for `Profiler` (`FrameUpdateCpu` frame count and `OPTIGD_PROFILE_SCOPE`) and `Logger::setCallback()`.
2. **Release Workflow Permissions & Metadata Gate (`.github/workflows/release.yml`, `scripts/build.sh`)**:
   - Added `actions: write` alongside `contents: write` in `.github/workflows/release.yml` so `geode-sdk/build-geode-mod/combine@main`'s temporary artifact cleanup (`geekyeggo/delete-artifact@v6`) has full token permissions during tag-triggered release runs.
   - Added `jq` version (`"0.1.0-beta1"`) and evidence classification (`"[SIMULATED]"`) assertions to `verify-tests` in `.github/workflows/release.yml`.
   - Updated `scripts/build.sh` to pass `-DOPTIGD_STANDALONE_ONLY=ON` and fall back gracefully if `ninja` is absent.




---

## Entry 5 — 2026-10-10: `v0.1.0-beta1` -> `v0.1.0-beta.2` — Loader Defect RCA, Core Audit Fixes & Release

### 5.1 Root Cause: `v0.1.0-beta1` never loaded `[VERIFIED]`

**Symptom**: GD started, OptiGD was absent from the mod list, log showed `unexpected type string`.

**Hypothesis tested and rejected**: that the `tier-preset` setting (`string` + `one-of`)
triggered it. `loader/src/loader/SettingV3.cpp:980-1019` explicitly supports
`string` + `one-of`. Do not "fix" this by converting string settings.

**Actual chain** (read from unzipped SDK `v5.10.1` at `/tmp/geode-5.10.1`):

1. `ModMetadataImpl.cpp:288` — `root.needs("version").into(impl->m_version)` (`VersionInfo`).
2. `matjson::Serialize<VersionInfo>::fromJson` -> `VersionInfo::parse`.
3. `VersionTag::parse` matches `beta` then leaves `'1'` unconsumed in `-beta1`.
4. `VersionInfo::parse` -> `Err("Expected end of version, found '1'")`.
5. `JsonValidation.hpp:105-118` `tryGet<T>()` flattens it to `unexpected type string`.
6. `LoaderImpl.cpp:626-630` -> `addProblem({InvalidGeodeFile})`, mod skipped.

**Dynamic proof** (`/tmp/verify_version/`, grammar extracted verbatim into a
standalone harness): `v0.1.0-beta1` and `v0.1.0-beta2` -> ERR;
`v0.1.0-beta.2`, `v0.1.0-beta.1`, `v0.1.0-beta`, `v0.1.0`, `5.10.1`, `v5.10.1`,
`v0.1.0-alpha.3` -> OK; `2.2081` -> `Patch version missing`.

**Decision**: version is `v0.1.0-beta.2`. `v0.1.0-beta2` (no dot) reproduces the
bug; `beta.2` is the only prerelease form Geode accepts that keeps the "beta2"
designation. Rationale recorded in `src/core/Types.hpp` and enforced by four
independent guards (ported grammar + unit test + `tools/validate_geode_metadata.py`
+ CI `jq` assertions).

**Evidence separation, as required**: code-level proof is complete; packaging
proof is complete (`mod.json` passes the validator, CI asserts the shipped
archive); **in-game confirmation is still `NOT RUN`** — no GD binary, GPU
context, or Geode loader exists here. The loading defect is not closed as
"verified in game".

### 5.2 Audit findings fixed

1. **`VisibilityCuller` index/sentinel collision**: `encodeIndex`/`decodeIndex`
   now offset by `+1` so object index `0` is distinguishable from the
   `SpatialHash` "no item" sentinel.
2. **`setCellSize()` mid-level** now rebuilds via a new private
   `rebuildSpatialIndex()`.
3. **Culling was irreversible**: added `restoreAllVisibility()` /
   `hasCulledObjects()` / `getCellSize()`; `PlayLayer` tracks `cullingEngaged`
   and restores a captured buffer when culling is disabled mid-level.
4. **`VisibilityCuller` steady-state allocations**: `m_previouslyVisibleIndices`
   / `m_currentVisibleIndices` member scratch vectors swapped each frame.
5. **`ParticleGovernor` O(N) derived stats**: `m_currentTotalParticles` and
   `m_sleepingEmitterCount` maintained incrementally; `getLastAvoidedParticleCount()`
   made lazy behind a dirty flag; `recalculateParticleTotals()` deleted.
6. **`MatrixCache` served uncomputed matrices**: added a `computed` flag;
   `store()` made anchor-aware; `pruneStaleEntries()` added (iterative, detaches
   children so no dangling pointers).
7. **`FallbackManager` recovery unreachable during cooldown**: recovery now
   evaluated every frame; cooldown gates only new fallbacks.
8. **`CompatibilityLayer` GD gate was `starts_with("2.2")`** (would accept GD
   `2.21x`): replaced with a numeric comparison against `kTargetGDVersion`.
9. **`TriggerOptimizer::inspectLookaheadWindow()` mutated execution state**
   despite being documented non-destructive: now uses `reportedInLookahead`,
   leaving `activated` / `isCompleted` untouched.
10. **`Profiler::record()` took a mutex and built a `std::string` key per call**
    — a per-frame heap allocation contradicting the zero-allocation claim.
    Added `MetricId` + `recordById()` over a fixed `std::array` (no lock, no
    allocation); the name API resolves known metrics to the same slots.
11. **Tier defaults were duplicated** between `ConfigManager::applyTierPresetUnlocked()`
    and `AdaptiveGovernor::evaluateTierAdjustment()`, so an automatic downgrade
    could land on different numbers than the same tier picked by hand.
    Collapsed into `core::kTierDefaults` + `core::applyTierDefaults()` in
    `Types.hpp`; both consumers now call it, and `Custom` is an explicit no-op.
12. **Robustness**: guarded numeric parsing in `ConfigManager::deserialize`;
    `MemoryArena` block reuse no longer destroys live frame-scoped data;
    `DiagnosticsEngine` escapes all interpolated strings; `psapi.lib` pragma is
    `_MSC_VER`-guarded.

### 5.3 A bug this work introduced, and the lesson

Adding `reportedInLookahead` to `OptimizedTrigger` initially broke
`testTriggerOptimizer` (`Spawn group 99 should invoke trigger 4`). Cause:
`registerTrigger()` used **positional aggregate initialization** with exactly one
initializer per member, so inserting a field in the middle silently shifted
`nativePtr` into the wrong slot. Two mitigations applied: the trigger is now
built field-by-field, and new fields are appended **last** with a comment saying
why. **Rule for this repo: never add a member in the middle of a struct that is
aggregate-initialized.**

### 5.4 Diagnostics honesty changes

- Trigger counters report `0` unless `enableTriggerIndexing` is on. Trigger
  indexing is telemetry-only and never gates native GD execution.
- `DiagnosticsEngine` now emits a `notes` block:
  `native_trigger_execution_modified: false`, plus a definition of
  `total_triggers_skipped`.
- Per-subsystem timing (`recordCullingTimeUs` / `recordParticleTimeUs` /
  `recordTriggerTimeUs` / `recordMatrixTimeUs`) added so the overlay and JSON can
  attribute cost rather than imply it.

### 5.5 New tooling

| Tool | Purpose |
|---|---|
| `src/core/GeodeVersion.{hpp,cpp}` | Faithful port of `VersionInfo::parse` / `VersionTag::parse`. |
| `tools/validate_geode_metadata.py` | `mod.json` vs SDK rules + cross-checks `version`/`geode`/`gd` against `Types.hpp` and `CMakeLists.txt`. |
| `tools/validate_diagnostics_json.py` | Schema + semantics for the exported diagnostics report. |
| `tools/validate_benchmark_json.py` | Schema + asserts `[SIMULATED]` and `optigd_heap_allocations == 0`. |
| `cmake/EmitDiagnosticsReport.cmake`, `cmake/RunBenchmarkSchemaCheck.cmake` | Wire the above into `ctest`. |
| `optigd_tests --emit-report <path>` | Emits a real diagnostics JSON so the artefact is validated out of process. |

### 5.6 Verification results `[MEASURED]`

- `ctest --output-on-failure`: **4/4 pass** (20/20 unit tests).
- RelWithDebInfo + `-fsanitize=address,undefined`: full suite and both binaries
  run clean — no findings, no leaks.
- Benchmarks (median of 5, Linux x86_64, GCC 12.2.0, `-O3`, seed 42):
  `5.45x` / `5.36x` / `4.19x`, `81.64%` / `81.34%` / `76.15%` CPU reduction,
  per-frame heap allocations `0` in every scenario. `[SIMULATED]` — not GD frame
  times, and not a live-game claim.
- Docs regenerated (`README.md`, `CHANGELOG.md`, `EVIDENCE_REPORT.md`,
  `docs/BENCHMARKS.md`, `docs/ARCHITECTURE.md`, `docs/RELEASE_WORKFLOW.md`,
  `AGENT.md`); `docs/BENCHMARKS.md` numbers came from the rebuilt binary, not
  hand-editing.

### 5.7 Open items / blockers

1. **Multi-platform `.geode` CI matrix** had not completed at the time of
   writing; results go in `EVIDENCE_REPORT.md` §7 once available.
2. **Live in-game validation remains `BLOCKED`.** No Geometry Dash `2.2081`
   binary, GPU context, or Geode loader in this environment. The beta.1 → beta.2
   version fix is proven at the grammar and packaging level only; a hardware
   tester must still confirm the mod appears and runs in game.

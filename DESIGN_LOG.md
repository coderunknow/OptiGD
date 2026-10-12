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

### 5.7 CI verification (PR #6, run `38060134568`, commit `03ec664`)

All 6 jobs passed:

| Job | ID | Result |
|---|---|---|
| `Core Engine Tests & Benchmarks (ubuntu-latest)` | `114236537673` | PASS |
| `Core Engine Tests & Benchmarks (windows-latest)` | `114236537804` | PASS |
| `Geode Mod (Windows x64)` | `114236820940` | PASS |
| `Geode Mod (Android64 (arm64-v8a))` | `114236820916` | PASS |
| `Geode Mod (Android32 (armeabi-v7a))` | `114236820910` | PASS |
| `Package Combined .geode Artifact` | `114237310787` | PASS (470,639 bytes) |

The packaging jobs are hard-gated on `jq -e '.version == "v0.1.0-beta.2"'` and
`jq -e '.id == "coderunknow.optigd"'` run against `mod.json` extracted from inside
the built `.geode`, and the combined job greps for all three binaries. That is the
packaging-level half of the loading-defect fix.

Two caveats recorded honestly:

1. `Kilo Code Review` reports `action_required` — *"Review could not start because
   the account has insufficient credits."* That is a billing condition on a
   third-party service, not a code finding. `CodeRabbit` passed.
2. The combined artifact could not be downloaded into this environment (egress is
   restricted to `github.com`, `codeload.github.com`, `api.github.com`,
   `registry.npmjs.org`, `pypi.org`, `files.pythonhosted.org`; the artifact is
   hosted on `*.blob.core.windows.net`). CI job logs are likewise unreachable
   (`results-receiver.actions.githubusercontent.com`). The archive contents are
   therefore attested by CI's own in-archive assertions, not by an independent
   local re-inspection.

### 5.8 Open items / blockers

1. **Live in-game validation remains `BLOCKED`.** No Geometry Dash `2.2081`
   binary, GPU context, or Geode loader in this environment. The beta.1 → beta.2
   version fix is proven at the grammar level (extracted-grammar harness), at the
   unit-test level, and at the packaging level (CI `jq` gates on the shipped
   archive) — but **not** by observing the mod load in a real client. A hardware
   tester must still confirm the mod appears and runs in game. This is the one
   criterion that cannot be closed from here, and it is left open rather than
   reported as a pass.

---

## Entry 6 — 2026-10-11: `v0.1.0-beta.3` — In-Game Benchmark Mode, Telemetry Integrity & Bug Fixes

> Status of this entry: **IN PROGRESS**. It is updated as substantive work lands, not only at the end.
> Sections marked `[PLANNED]` describe intent; sections marked `[IMPLEMENTED]` describe code that
> exists in the tree; sections marked `[MEASURED]` carry command output produced in this environment.

### 6.1 Original request (verbatim intent, condensed)

Build a usable `v0.1.0-beta.3` release candidate that introduces a **complete in-game Benchmark
Mode** for measuring OptiGD's actual runtime behaviour, comparing optimization configurations,
reviewing saved results and exporting evidence. Target: Geometry Dash `2.2081`, Geode `v5.10.1`
(to be verified against the repo and build environment), Windows x64 + Android64 + Android32.

Required outcomes:

1. A benchmark workflow that is **reachable and usable inside the real Geode/GD experience** — a
   JSON exporter, a CLI utility, or a settings-only toggle is explicitly insufficient.
2. Start a 60 s **baseline** measurement and a 60 s **optimized** measurement; observe
   idle/running/completing/completed/cancelled/invalid state; see remaining time; auto-finalize at
   the configured duration without leaving the level; review saved runs; compare baseline vs
   optimized; inspect config + measurement metadata; revisit and export previous results.
3. **Baseline = internal control**, applied through a *safe transient runtime configuration* that
   never overwrites the user's saved preferences and never leaves features disabled after a run
   ends, is cancelled, or errors. Native trigger execution, player physics and collision semantics
   must be untouched. The baseline is *not* vanilla GD; OptiGD's hooks/instrumentation may still
   cost time and that must be disclosed.
4. **Telemetry correctness is release-critical**: audit the 3,600-frame diagnostics history vs the
   cumulative counters that outlive it, the fact that `DiagnosticsEngine::reset()` historically ran
   at mod init rather than per level load, high-refresh-rate windows, percentile ordering/consistency,
   FPS/frame-time units, empty/interrupted/short sessions, timestamp sources, overhead vs total
   frame time, and validity across tier changes/fallbacks. Bounded memory, no per-frame heap churn.
5. **Persistent, session-specific results** in the local OptiGD save dir with stable session ids,
   schema/version metadata, multiple retained sessions (no overwriting), safe/atomic serialization,
   and in-game inspection/comparison (no manual JSON or Python required for the normal workflow).
   Zero network/telemetry upload.
6. **Meaningful comparison**: both values + absolute/relative delta, correct metric direction,
   zero-denominator and missing-data handling, incompatibility warnings instead of manufactured
   equivalence, improved/worsened/unchanged summary with a noise threshold, no unsupported
   statistical-significance claims, and a documented repeated-run protocol.
7. **Internal telemetry vs externally measured presented frames** must be clearly distinguished,
   with a verified Windows x64 CapFrameX procedure and honest reporting of what was *not* run.
8. Audit and fix relevant bugs beyond the new UI, with regression tests, preserving design intent.
9. Version metadata bumped to the Geode-accepted `v0.1.0-beta.3` form across every reference, with
   Win64/Android64/Android32 support intact.
10. Tests that exercise behaviour (state machine, telemetry, reports, persistence, comparison,
    backward compatibility, regressions), plus real build/package verification.
11. Documentation updated to describe the implementation, not the intention.
12. Dedicated branch + **Draft PR**; **no merge, no tag, no official release**.

Explicit non-goals from the request: do not manufacture a better-looking FPS number; do not
fabricate gameplay/inputs/physics to obtain a result; do not claim an FPS gain merely because the
benchmark system works; keep "unit tests pass", "package builds" and "measured in a real game" as
separate evidence categories.

### 6.2 Verified starting state of the repository `[MEASURED]`

Inspected rather than assumed:

- Branch `arena/f6290987-optigd` at `0153164` ("Merge v0.1.0-beta.2 …"), clean tree, shallow clone
  (`git rev-parse --is-shallow-repository` → `true`), single reachable commit.
- Existing PRs: #1,#2,#3,#5,#6 **merged**; **#4 still OPEN as a Draft** (`arena/21ca20f2-optigd`,
  "OptiGD v0.1.0-beta1 (Draft: gameplay benchmark blocked)") — superseded by #5/#6.
- Toolchain present in this sandbox: `g++ 12.2.0`, `python3 3.11.2`, `jq`, `unzip`, `git`, `gh`
  (authenticated as `coderunknow`). **`cmake`/`ninja` were absent**; installed into a local venv
  (`cmake 4.4.4`, `ninja 1.13.2`) at `/home/user/.venv` — outside the repo, not committed.
- Baseline reproduction of the documented commands:
  `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPTIGD_STANDALONE_ONLY=ON` → configure OK;
  `cmake --build build` → OK; `ctest --test-dir build --output-on-failure` → **4/4 pass**
  (`OptiGDUnitTests`, `OptiGDDiagnosticsJson`, `OptiGDGeodeMetadataValidation`,
  `OptiGDBenchmarkJsonSchema`).
- Egress is limited to `github.com`, `codeload.github.com`, `api.github.com`, `registry.npmjs.org`,
  `pypi.org`, `files.pythonhosted.org`. `release-assets.githubusercontent.com` is **blocked**
  (verified: `curl` → `SSL_ERROR_SYSCALL`), so the Geode CLI, `bindgen`, the Geode target binaries
  and CI artifacts **cannot** be downloaded here. A local `.geode` build is therefore impossible;
  Geode-target compilation must be verified through GitHub Actions (which does run on `arena/**`
  pushes per `.github/workflows/build.yml`).
- Downloaded and read, for API verification only (kept in `/tmp`, not in the repo):
  `geode-sdk/geode @ v5.10.1` and `geode-sdk/bindings @ main` (which contains `bindings/2.2081/`).

### 6.3 Audit findings against the beta.3 requirements `[VERIFIED by source read]`

Confirmed defects in the tree as of `0153164` (each traced to file + line, not inferred from docs):

| # | Finding | Where | Why it matters |
|---|---|---|---|
| A | `DiagnosticsEngine` keeps `m_maxHistoryFrames{3600}` while `m_cumulativeFrames` and every other cumulative counter grow without bound | `src/diagnostics/DiagnosticsEngine.{hpp,cpp}` | `computeSummary()` computes mean/P50/P95/P99 from ≤3600 retained samples but reports `total_frames` from the unbounded counter. At 144 Hz, 3600 frames ≈ 25 s, so a "60 s" report silently describes two different windows. |
| B | `DiagnosticsEngine::reset()` is called from `OptiGDManager::initialize()` (once at mod load) and **not** from `onLevelLoad()` | `src/core/OptiGDManager.cpp:38` vs `onLevelLoad()` | The level-exit report aggregates every level and every attempt since the mod loaded. Prior-level data contaminates the next report. |
| C | Telemetry is driven from `GJBaseGameLayer::update` with no check that a `PlayLayer` is active | `src/geode/Hooks.cpp` | `GJBaseGameLayer` is also the base of `LevelEditorLayer`; frames recorded while editing are folded into gameplay telemetry. |
| D | `m_currentStats.culledParticles` falls back to `getLastCulledEmitterCount() * 32` when the measured value is 0 | `src/core/OptiGDManager.cpp::endFrame()` | A fabricated ×32 estimate is reported in the same field as a measured count — exactly the "internally estimated value presented as measured" failure the request forbids. |
| E | `matrixCacheHits/Misses` per-frame deltas are computed as `uint64_t` then `static_cast<uint32_t>` | `src/core/OptiGDManager.cpp::endFrame()` | Truncation on a `uint64_t` field; wrong values for large deltas. |
| F | `updateCpuTimeUs` is a **wall-clock** span (`endFrame - beginFrame`), not thread CPU time | `src/core/OptiGDManager.cpp::endFrame()` | The name implies CPU time. Under scheduling pre-emption the two diverge; the report must say what it is. |
| G | `optigdOverheadUs` accumulators can be written outside an open frame window | `recordParticleTimeUs` called from the `CCParticleSystem` hook | Particle updates that occur while no `beginFrame()`/`endFrame()` window is open are attributed to the *next* frame. |
| H | `exportJsonReportToFile()` opens the destination with `std::ofstream` (truncating) and returns `true` without checking the stream state | `src/diagnostics/DiagnosticsEngine.cpp` | A failed/partial write destroys the previous valid report and is still reported as success. |
| I | `generateJsonReport()` streams raw `double`s with `std::fixed` | `src/diagnostics/DiagnosticsEngine.cpp` | A NaN/Inf (possible from a 0-sample or pathological window) emits `nan`/`inf` → **unparseable JSON**. |
| J | Percentile indices use `floor(n*p)` clamped to `n-1` | `DiagnosticsEngine.cpp`, `AdaptiveGovernor.cpp` | For small `n` this is biased toward the maximum (`n=20` → P95 index 19 == max). Nearest-rank is the standard, less-biased estimator and must be documented. |
| K | `ConfigManager` has a single `m_config` written by both `syncFromSettings()` (Geode settings listeners) and `OptiGDManager::endFrame()` (adaptive tier / fallback write-back) | `src/config/ConfigManager.cpp` | There is no place to hold a **transient benchmark override**: any settings-listener firing, or `PlayLayer::init`'s `syncSettingsWithConfig()`, would clobber a baseline configuration mid-session, and restoring it would clobber the user's saved settings. |
| L | No session-scoped measurement window exists at all; there is no in-game benchmark UI, no persistent per-session store, and no A/B comparison in-game | whole repo | `docs/BENCHMARKS.md` §3 describes a fully **manual** protocol (edit settings by hand, play, exit the level, copy a JSON file, run Python). This is the central problem beta.3 exists to fix. |
| M | `Profiler::getById()` takes `m_mutex` while `recordById()` does not | `src/profiling/Profiler.cpp` | Documented as single-threaded-reader-safe; the lock in the reader gives false confidence and costs a mutex on the display path. Behaviour is acceptable but the contract must be stated honestly. |
| N | `DiagnosticsSummary` exposes no sample-count/window/duration fields | `src/diagnostics/DiagnosticsEngine.hpp` | A report cannot disclose the window its percentiles came from, so finding (A) is invisible to the reader. |

Items confirmed **not** to be defects (checked, so they are not "fixed" into regressions):

- The Geode `VersionInfo` grammar analysis in Entry 5 is still accurate: `v0.1.0-beta.3` is the
  accepted form and `v0.1.0-beta3` is rejected (`tools/validate_geode_metadata.py` + `GeodeVersion.cpp`).
- `PauseLayer` is `[[link(android)]]` in `bindings/2.2081/GeometryDash.bro` and exposes
  `virtual void customSetup()`, so a pause-menu entry point is valid on Win64/Android32/Android64.
- `PlayLayer` exposes `m_isPaused`, `m_hasCompletedLevel`, `m_attemptTime`; `GJBaseGameLayer`
  exposes `m_level` (`GJGameLevel*`) and `m_attempts`; `GJGameLevel` exposes `m_levelID`
  (`geode::SeedValueRSV`) and `m_levelName` — enough for *best-effort* level identity without
  inventing identifiers.
- Trigger execution remains native: `TriggerOptimizer` only inspects (`reportedInLookahead`), and
  the diagnostics `notes.native_trigger_execution_modified` field is `false`. Preserved as-is.

### 6.4 User decisions taken before implementation (asked, answered)

| Decision | Answer | Consequence |
|---|---|---|
| In-game entry point | **Pause-menu button + in-level HUD** | Hook `PauseLayer::customSetup()` to add an "OptiGD Benchmark" button opening a `geode::Popup`; a compact HUD label in `PlayLayer` shows state/remaining time while a session runs. No clickable widget over the play area. |
| What the 60 s window counts | **Accumulated active gameplay time** | The window advances only by real inter-frame intervals while the game loop runs; pauses/alt-tabs/>250 ms stalls are *excluded* from the window but counted as `gap_events` and recorded as integrity warnings. Wall-clock elapsed is recorded separately so the ratio is visible. |
| Death / attempt restart mid-session | **Mod setting, default = stop** | `benchmark-stop-on-death` (default `true`) finalizes the session as `incomplete` with an `interruptions:[level_reset]` record; when `false` the session continues but carries a `segment_not_contiguous` warning and a `reset_count`. |
| A/B pairing | **Manual sessions + guided protocol** | Two explicit buttons (baseline / optimized). The panel shows a protocol checklist, and the comparator refuses or warns when the two sessions are not materially comparable. |
| Stale Draft PR #4 | **Leave it alone** | Not touched; noted here and in the handoff so the maintainer can close it. |

Additional defaults chosen by the agent (documented here because they are measurement semantics,
not cosmetics):

- Session duration is configurable via `benchmark-duration-seconds` (15–600, **default 60**) so the
  required 60 s workflow is the default while longer captures remain possible.
- Sessions are stored under `<GD save dir>/geode/mods/coderunknow.optigd/benchmarks/` as one JSON
  file per session plus a rebuildable `index.json`. **No automatic pruning** — the request forbids
  silent loss of existing results.
- The legacy `optigd_diagnostics.json` keeps every existing key and adds a
  `report_schema_version` field. v1 (field absent) is still accepted by the validator; v2 requires
  the new window-disclosure keys. This is an explicit, tested schema transition rather than a
  breaking change.

### 6.5 Implementation plan `[PLANNED]`

New platform-agnostic core (all unit-testable in this sandbox, no Geode dependency):

- `src/core/JsonValue.{hpp,cpp}` — minimal JSON reader/writer so saved sessions can be re-opened
  in-game without Python.
- `src/diagnostics/SampleWindow.{hpp,cpp}` — bounded measurement window: fixed-capacity ring of
  frame times + fixed-bin histogram + exact accumulators; O(1) push, no per-sample heap churn;
  declares whether percentiles are `exact` or `histogram-approximate`.
- `src/benchmark/BenchmarkTypes.hpp` — modes, states, statuses, integrity-warning codes.
- `src/benchmark/BenchmarkSession.{hpp,cpp}` — the immutable session record + JSON round-trip.
- `src/benchmark/BenchmarkController.{hpp,cpp}` — lifecycle state machine, transient config
  override, gap/pause/reset/exit handling, auto-finalization.
- `src/benchmark/BenchmarkStore.{hpp,cpp}` — atomic persistence, index, listing, tolerant loading.
- `src/benchmark/BenchmarkCompare.{hpp,cpp}` — A/B comparison with direction, deltas, noise
  threshold and compatibility gating.
- `src/ui/BenchmarkPanel.{hpp,cpp}` — platform-agnostic text for HUD, panel, results list,
  comparison and protocol, so the Geode layer stays thin.
- `src/geode/BenchmarkUI.{hpp,cpp}` — the `geode::Popup` subclass and the `PauseLayer` button.

Fixes to existing modules: `ConfigManager` (transient override layer), `DiagnosticsEngine`
(SampleWindow + schema v2 + atomic write + finite-number guard + window disclosure),
`OptiGDManager` (reset on level load, thread-CPU timing, gap classification, remove the ×32
estimate, remove the uint32 truncation, frame-open guard), `AdaptiveGovernor` (shared percentile
helper), `Platform` (thread CPU time, architecture string), `Hooks.cpp` (PlayLayer gate, level
identity, benchmark driving, HUD, pause-menu button).

### 6.6 What was actually built `[VERIFIED by build + tests]`

Entry 6.5 planned nine new modules. All nine exist, compile clean (`-Wall -Wextra -Wpedantic`,
zero warnings) and are covered by tests. Sizes as committed:

| Module | Lines | Role |
|---|---|---|
| `src/core/JsonValue.{hpp,cpp}` | 148 + 659 | Minimal JSON reader/writer (free functions `json::parse` / `json::serialize`) so a saved session can be re-opened in-game without Python. Non-finite doubles serialize as `null` and set a `sawNonFinite` flag. |
| `src/diagnostics/SampleWindow.{hpp,cpp}` | 258 + 439 | Bounded measurement window: fixed-capacity ring + fixed-bin histogram + exact accumulators. O(1) push. **States** whether its percentiles are `exact` or `histogram_approximate`, and owns the single percentile definition (`medianFromSorted`, `percentileFromSorted`). |
| `src/benchmark/BenchmarkTypes.{hpp,cpp}` | 179 + 179 | Modes, states, statuses, the fixed integrity-warning vocabulary, level identity, environment, and the evidence-classification tags (`evidence::kMeasuredInGame` / `kSimulated`). |
| `src/benchmark/BenchmarkSession.{hpp,cpp}` | 167 + 768 | The session record: window, statistics, the configuration that was actually in force, integrity counters, warnings, measurement definitions, limitations. JSON round-trip with schema/version gating. |
| `src/benchmark/BenchmarkStore.{hpp,cpp}` | 120 + 404 | Atomic persistence under `<GD save dir>/geode/mods/coderunknow.optigd/benchmarks/`, id allocation, tolerant listing, rebuildable `index.json`. Never overwrites prior evidence. |
| `src/benchmark/BenchmarkController.{hpp,cpp}` | 240 + 682 | Lifecycle state machine: start / frame routing / pause+stall exclusion / reset / exit / cancel / auto-finalize, verified transient config override, persist-with-retry. |
| `src/benchmark/BenchmarkCompare.{hpp,cpp}` | 95 + 394 | A/B comparison: direction per metric, absolute + relative deltas, ±2 % noise threshold, compatibility gating, and a text renderer. `significanceClaimed` is hardcoded `false`. |
| `src/ui/BenchmarkPanel.{hpp,cpp}` | 87 + 562 | All user-facing text (HUD, main, results, detail, comparison, protocol, external procedure) plus pagination — platform-agnostic, so the Geode layer stays thin. |
| `src/geode/BenchmarkUI.{hpp,cpp}` | 155 + 725 | The `geode::Popup` subclass (460×296, six views), the `PauseLayer` button, the runtime bridge, notifications, and the environment reader. **Geode-only: compiled by CI, not locally.** |

New tooling:

- `optigd_tests --emit-sessions <dir>` drives the real controller with a synthetic clock and a
  temporary store, then writes `sessions/<id>.json` ×2, `sessions/index.json` and `comparison.txt`.
- `cmake/EmitBenchmarkSessions.cmake` + `tools/validate_benchmark_session_json.py` make that
  artifact a ctest (`OptiGDBenchmarkSessionJson`): the validator accepts a file or a directory and
  checks structure **and** the cross-field invariants (frame budget, percentile ordering, state vs.
  data, baseline disclosure, evidence classification, index consistency).
- `tests/UnitTests.cpp` grows from 20 to **50** test functions; `ctest` grows from 4 to **5** checks.

### 6.7 Defects found while building beta.3 (append to the 6.3 table)

| # | Finding | Where | Why it matters / what was done |
|---|---|---|---|
| O | The frame delta was accepted only when `100 < us < 250000`; otherwise beta.2 substituted GD's own clamped `dt * 1e6` | `src/geode/Hooks.cpp`, `OptiGDManager::endFrame()` | GD clamps `dt`, so every stall ≥ 250 ms was silently replaced by a plausible frame time: the exact frames that make a level feel bad disappeared from the telemetry. Now the raw uncapped monotonic interval is measured, classified against `core::kStallThresholdMs`, recorded as a gap and excluded from the window. |
| P | `json::Value::asDouble(fallback)` returned the fallback for a non-finite stored double | `src/core/JsonValue.cpp` | The serializer asks for the value in order to decide how to write it, so substituting `0.0` turned "this metric was not a number" into "this metric was zero" — a plausible-looking measurement that never happened — and `sawNonFinite` never fired. `asDouble` now returns the stored double exactly; sanitizing stays the writer's job (`formatNumber` → `null` + flag). Found by `testJsonValueRoundTripAndEscaping`. |
| Q | `BenchmarkSession::toJson()` hardcoded `"evidence_classification": "[MEASURED-IN-GAME]"` | `src/benchmark/BenchmarkSession.cpp` | The same serializer is used by the in-game capture **and** by the standalone schema tools, so the artifacts `--emit-sessions` writes for ctest claimed to have been measured inside a live Geometry Dash process. They were not. The tag is now a field stamped by the controller from the runtime it is attached to (`setLiveGameRuntime`, true only in `installBenchmarkRuntimeBridge`), it defaults to `[SIMULATED]`, and loading a file may only **downgrade** it: an unrecognized tag is reported as `[SIMULATED]` with a warning. |
| R | `ConfigManager::setRuntimeOverride()` rewrote the override even when it returned `false` | `src/config/ConfigManager.cpp` | A refused re-arm destroyed the configuration the first caller needed restored later. It now returns early without touching state, and a separate `updateRuntimeOverride()` re-asserts the configuration of an override the session already owns. |
| S | The controller's re-apply path was dead code | `BenchmarkController::verifyOverride()` | It "re-installed" the override through `installOverride`, which refuses while an override is armed — so a session that detected a clobbered configuration could never put its own values back, and could never restore the user's config at the end. The bridge gained `updateOverride`, used by both the take-over path and the re-apply path; `testBenchmarkControllerVerifiesItsOwnOverride` exercises it end to end. |
| T | `BenchmarkSession::fromJson()` never re-derived warnings | `src/benchmark/BenchmarkSession.cpp` | A hand-edited or truncated file could present itself as `completed` with zero samples. Loading now calls `refreshDerivedWarnings()`, so a record can never claim more than its own data supports. |
| U | Terminal state was derived from the data without distinguishing a user cancellation | `BenchmarkController::finalize()` | A capture cancelled after 80 ms was relabelled `invalid`, which hides the one fact the user actually knows (they stopped it). `cancelled` is now preserved; `invalid` is reserved for captures that ended on their own with too little data. Both remain non-comparable, and both still carry `no_samples_recorded` / `capture_shorter_than_target`. |
| J (resolved) | `AdaptiveGovernor` indexed percentiles at `floor(n * p)` | `src/adaptive/AdaptiveGovernor.cpp` | Fixed as planned in 6.5: it now calls `SampleWindow::medianFromSorted` / `percentileFromSorted`. The two definitions agree whenever `n * p` is fractional and differ by exactly one sample when it is an integer — the common case for a round window, where the governor's "p95" was the *slowest* frame in the window (n = 20 → index 19). `testGovernorPercentilesUseTheSharedDefinition` locks it. |

### 6.8 Measurement semantics that were decided during implementation

These are not cosmetics; each one changes what a number in a session record means.

- **The window counts active gameplay, not elapsed time.** `captured_duration_ms` is the sum of
  accepted inter-frame intervals. Paused frames (pause menu open, level intro) are excluded and
  accumulated in `paused_ms`; stalls (≥ `core::kStallThresholdMs`) are excluded and counted as
  `gap_events`. `wall_duration_ms` is the monotonic start→end span. A "60 second" capture is 60 s
  of play, and the record shows both numbers so the ratio is visible.
- **A paused frame is indistinguishable from a played one by interval alone.** With the pause menu
  open, `GJBaseGameLayer::update` still runs at the display rate. Only the explicit pause signal
  (`OptiGDManager::setFramePaused`, driven from `PlayLayer::m_isPaused`) identifies it — without
  that, a session could be "completed" without playing.
- **Every offered frame is accounted for exactly once**:
  `frames_observed == frames_counted + frames_excluded + frames_rejected`. The Python validator
  enforces this on the emitted artifact, not just the C++ tests.
- **The baseline is an internal control, and the record says so.** `makeBaselineConfig()` disables
  culling, the particle governor, trigger indexing, the matrix cache and the adaptive governor, and
  keeps the diagnostics overlay, the exporter, the tier machinery and the numeric settings. OptiGD's
  hooks and instrumentation still run. `mode_display` reads
  `BASELINE (OptiGD installed, optimizations in passthrough)`, `features_disabled_for_baseline`
  lists the CompatibilityLayer feature ids that were actually forced into passthrough (read back
  from the applied config, never from the setting labels), and `measurement_definitions.baseline_mode`
  states that this is not vanilla Geometry Dash.
- **Percentiles are defined once, in one place, and the record states the method.**
  P50 = median (mean of the two central values for even `n`); P95/P99 = nearest-rank
  (`rank = ceil(p * n)`, `index = rank - 1`). When the ring overflows and only the histogram
  remains, `percentile_method` becomes `histogram_approximate`, the panel renders those values with
  a `~`, and `percentiles_approximated` is added to the warnings.
- **Nothing claims significance.** `ComparisonResult::significanceClaimed` is `false` by
  construction and the rendered text says a single pair of captures cannot separate an optimization
  effect from run-to-run variability. Relative change from a zero baseline is reported as undefined
  with an explanatory note, never as a number.
- **Comparisons refuse rather than conclude.** Blocking issues: no samples on either side, a
  non-comparable state, both sides the same mode, platform/architecture mismatch, level-identity
  mismatch, duration differing by more than 25 %, and overlapping capture windows. Non-blocking but
  surfaced: duration differing by more than 10 %, and every integrity warning on either side.
- **Evidence is never overwritten.** `BenchmarkStore::save()` refuses to replace an existing file
  unless the bytes are identical (`errorCode == "already_exists"`); ids are allocated by probing the
  directory; ids are validated against an allow-list so an id can never escape the store directory;
  writes are atomic (temp + rename) so a crash cannot leave a half-written record; there is no
  pruning of any kind.

### 6.9 Verification results for beta.3 `[MEASURED]`

Environment: Debian 12 x86_64 container, g++ 12.2.0, cmake 4.4.4 + ninja 1.13.2,
`cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPTIGD_STANDALONE_ONLY=ON`.

| Check | Command | Result |
|---|---|---|
| Unit + integration suite | `./build/optigd_tests` | **PASS** — 50/50, exit 0, no warnings at build time |
| Full ctest | `ctest --test-dir build --output-on-failure` | **PASS** — 5/5 |
| Session artifact schema | `python3 tools/validate_benchmark_session_json.py <dir>` | **PASS** — 2 sessions (baseline, optimized), 0 errors |
| Validator mutation check | 13 hand-tampered artifacts (frame budget, false `[MEASURED-IN-GAME]`, `completed` with 0 samples, percentile order, missing key, captured > wall, override not applied, baseline disclosure removed, NaN, unknown warning code, index count, duplicate id, comparison without provenance) | **13/13 detected**, each exit 1 with a specific message — the validator is not a no-op |
| Geode-only sources | `src/geode/Hooks.cpp`, `src/geode/BenchmarkUI.cpp` | **NOT RUN locally** — no Geode SDK/CLI/bindgen binaries are reachable from this sandbox (release-asset downloads are blocked). Every OptiGD API they call was checked by hand against the headers; compilation is verified by CI on Win64 / Android64 / Android32. |
| Live in-game Benchmark Mode | — | **BLOCKED / NOT RUN** — no Geometry Dash 2.2081 binary, no GPU/display, no Geode loader, no Windows host, no Android device or emulator in this environment. |
| CapFrameX external capture (Windows x64) | — | **BLOCKED / NOT RUN** — requires a Windows host with a GPU and the CapFrameX binary. The procedure is documented (`BenchmarkPanel::formatExternalProcedure()`, the popup's External view, `docs/BENCHMARKS.md`); no external numbers exist and none are implied. |

The `--emit-sessions` artifact is real OptiGD code (controller, session, store, comparator) driven by
a synthetic clock into a temporary directory. It is stamped `[SIMULATED]` in every record and in
`comparison.txt`, and the validator fails the build if that stamp is ever missing. It proves the
format and the invariants. **It is not a measurement of Geometry Dash, and the 50.00 → 62.50 FPS in
that file is a fixture, not a result.**

### 6.10 Open items / blockers at the end of this entry

- Live in-game validation of Benchmark Mode on Windows x64 (and Android) is **BLOCKED**: it needs a
  tester with GD 2.2081 + Geode v5.10.1. Acceptance criteria 1, 2 and 7 are therefore
  `[VERIFIED by code + test]` for the mechanism and `[NOT RUN]` for the live run.
- The external CapFrameX procedure is documented and reachable in-game, but no external capture
  exists, so criterion 6's external half is `[NOT RUN]` by construction.
- `src/geode/*` compiles only in CI. If CI reports an error there, it is a real defect and must be
  fixed rather than worked around by dropping a target.
- Stale Draft PR #4 (`arena/21ca20f2-optigd`) is left untouched per the user's decision; it is
  superseded by merged PRs #5 and #6 and should be closed by the maintainer.
- No merge, no release tag and no official release were performed for beta.3. The work is on
  `arena/f6290987-optigd` and handed over as a Draft PR.

### 6.11 Tooling hardened alongside the C++ (same honesty rules, out of process)

The C++ invariants are only half the guarantee: the artifacts are what a reader
actually sees, so the tools that check and render them were brought in line too.

- `tools/validate_diagnostics_json.py` now understands **`report_schema_version: 2`**.
  A v2 report must disclose its window (`sample_count`, `sample_window_ms`,
  `history_capacity`, `history_truncated`, `percentile_method`), its exclusion
  accounting (`excluded_samples`, `rejected_samples`, `stall_frames`,
  `paused_frames`), its thread-CPU availability, its `platform.architecture` and its
  `capture` block, and the validator enforces the same frame-budget identity the
  session validator does (`sample_count + excluded + rejected == total_frames`),
  plus `stall_frames + paused_frames <= excluded_samples`, percentile ordering,
  `avg_fps`/`min_fps`/`max_fps` consistency with the frame-time extremes, and
  "thread CPU time must be 0 when the platform cannot supply it" — a value the
  platform cannot measure must not be invented. A v1 report (no
  `report_schema_version`) still validates, so an older file on disk stays checkable.
  `active_duration_ms <= wall_duration_ms` is deliberately **not** asserted here:
  `--emit-report` feeds synthetic frame intervals while the wall clock is real, so
  that ordering legitimately inverts in the emitted artifact. It *is* enforced for
  benchmark sessions, where both numbers come from the same clock. Verified against
  13 tampered reports; all 13 detected.
- `tools/generate_report.py` now prints a **Measurement Window** section before any
  number, marks histogram-approximate percentiles with `~` (the same marker the
  in-game panel uses), keeps wall-clock update time and thread CPU time in separate
  rows, and states what each evidence classification means — including that a
  `[SIMULATED]` benchmark-suite artifact has no measurement window at all and is not
  a Geometry Dash frame time.
- `generate_report.py --compare` reports **comparability before deltas**: blocking
  conditions (a side with no samples, different platform/architecture, windows
  differing by more than 25 %) are printed as a "should NOT be compared" banner and
  the deltas are annotated as not quotable; non-blocking conditions (a v1 side, so
  the window is unknown; approximate percentiles; a truncated history; windows
  differing by more than 10 %; excluded stall/pause frames) are listed as warnings.
  The footer states that no statistical significance is claimed and that one pair of
  reports cannot separate an optimization effect from run-to-run variability. This
  is the legacy path; the in-game comparator is stricter and is the one to use.
- `tools/compare_capframex.py` (new) is described in §6.6 and in
  `EVIDENCE_REPORT.md` §5, including the limit of its own verification: its CSV
  fixtures are synthetic because no CapFrameX export could be obtained here.
- `.gitignore` gained `build-*/` so a sanitizer build directory (`build-asan`) cannot
  be committed by accident.

### 6.12 CI failure after the beta.3 push: root cause, diagnosis method, fix `[VERIFIED by CI]`

The beta.3 commit `4c686ef` was pushed to `arena/f6290987-optigd` and opened as draft
PR #7. Two `Build & Verify OptiGD` runs (`38107167242` for the push, `38107240434` for
the PR event) produced the same result:

| Job | Result | Duration of the failing step |
| --- | --- | --- |
| Core Engine Tests & Benchmarks (ubuntu-latest) | success | - |
| Core Engine Tests & Benchmarks (windows-latest) | success | - |
| Geode Mod (Android64, arm64-v8a) | **failure** | 90 s |
| Geode Mod (Android32, armeabi-v7a) | **failure** | 65 s |
| Geode Mod (Windows x64) | **failure** | 150 s |
| Package Combined .geode Artifact | skipped (dependency failed) | - |

Both standalone jobs compiling and testing every core source passed, so the break was
confined to the two translation units that only the Geode target builds:
`src/geode/Hooks.cpp` and the new `src/geode/BenchmarkUI.cpp`.

**Why the cause was not obvious.** The runner logs for those jobs are not retrievable
from this environment: `GET /actions/jobs/{id}/logs` redirects to
`productionresultssa8.blob.core.windows.net`, which is outside the network allowlist,
and the check-run annotations carry only `Process completed with exit code 1` with no
compiler diagnostics. The jobs API does expose per-step conclusions, which narrowed the
failure to the `Build Geode Mod Package` step (the composite
`geode-sdk/build-geode-mod@main` action) rather than to the `.geode` verification step
that follows it. Step durations were *not* diagnostic: beta.2's successful Geode builds
took 72-123 s, the same order as the failing ones, because the SDK loader is consumed as
prebuilt binaries and the action restores a compiler cache.

**How the cause was obtained.** A temporary workflow,
`.github/workflows/geode-diagnose.yml` (commit `8a3927a`), replays the composite
action's own configure and build commands - copied verbatim from its `action.yml` at
`a0f7676e`, including the NDK r29 toolchain file, `-DANDROID_ABI=arm64-v8a`,
`-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON`, clang/clang++ and `GEODE_TARGET_PLATFORM` -
but redirects stdout and stderr into a file and posts the filtered result as a comment
on PR #7, which *is* readable through the API. It is triggered only by a change to
`.github/geode-diagnose-trigger`, gates nothing, and is meant to be deleted once it has
served its purpose. Configure succeeded (`exit code 0`); the build failed with exactly
two failing objects and 13 errors:

```
FAILED: CMakeFiles/OptiGD.dir/src/geode/BenchmarkUI.cpp.o   (1 warning, 10 errors)
FAILED: CMakeFiles/OptiGD.dir/src/geode/Hooks.cpp.o         (3 errors)
src/geode/BenchmarkUI.hpp:135:28: error: no member named 'CCMenuItemSpriteExtra'
                                                     in namespace 'cocos2d'
```

**Root cause (defect V, introduced by beta.3).** `BenchmarkPopup` stored its buttons as
`std::map<Btn, cocos2d::CCMenuItemSpriteExtra*>`. `CCMenuItemSpriteExtra` is not a
cocos2d-x class; it is a Geometry Dash class that the generated bindings declare in the
**global** namespace as `class CCMenuItemSpriteExtra : public
cocos2d::CCMenuItemSprite`. The unqualified spelling used everywhere else in the file
was correct, so only this one declaration was wrong - and because it sits inside the
class body, the parse error destroyed the whole class definition. Every remaining error
is cascade, not independent damage: `popup->autorelease()`, `Popup::init(...)`,
`setTitle(...)`, three `addChild(...)` calls, `scheduleUpdate()`,
`CCMenuItemSpriteExtra::create(sprite, this, selector)` ("no known conversion from
`BenchmarkPopup*` to `cocos2d::CCObject*`") and `popup->show()` in `Hooks.cpp` all
failed only because `BenchmarkPopup` was no longer a well-formed `Popup` subclass.

**Fix.** `std::map<Btn, ::CCMenuItemSpriteExtra*> m_buttons;` with a comment recording
the namespace rule, plus `bool init() override;` to clear the accompanying
`-Winconsistent-missing-override` warning (the override is `cocos2d::CCLayer::init()`).
No other change was needed. Every other `cocos2d::`-qualified name in `src/geode/` was
audited against the SDK's cocos headers and the generated bindings
(`CCDirector`, `CCLabelBMFont`, `CCMenu`, `CCNode`, `CCObject`, `CCParticleSystem`,
`CCSize`, `SEL_MenuHandler`, `kCCPositionTypeGrouped` are all genuinely cocos2d).

**What the diagnostic run also established**, which is worth keeping because it
corrects assumptions that could not be checked locally:

- The Geode target is compiled with `-std=c++23`. `CMAKE_CXX_STANDARD 20` in
  `CMakeLists.txt` does not decide this: the SDK's
  `target_compile_features(geode-sdk PUBLIC cxx_std_23)` raises the mod target, and
  Geode 5.10.1 `static_assert`s on it. The standalone jobs still build at C++20.
- Android builds use NDK `29.0.14206865` clang with `-flto=thin -fvisibility=hidden`
  and `-Werror=return-type` (the only `-Werror`), targeting `android-23`.
- Bindings are *not* pinned: the action takes the current `main` of
  `geode-sdk/bindings`, so a bindings change can break this repository without any
  commit here. That is an accepted upstream property of the workflow, not something
  beta.3 introduced.
- Every core source, including all of the new benchmark, diagnostics and JSON modules,
  compiled cleanly for Android64 under clang/C++23/LTO before the build reached the two
  Geode-only files (`[44/57]`-`[53/57]` all succeeded).

**Honest limits of this fix.** It is verified by recompilation in CI, not by execution.
Nothing here ran Geometry Dash: the build stops at compilation and packaging, the link
step had not been reached when the error occurred, and no `.geode` produced by CI has
ever been loaded by the Geode loader in this project. Criterion 10 (all three targets
still build) can therefore only be claimed once the Geode jobs go green, and even then
it means "compiles and packages", never "works in game".

### 6.13 The fix verified in CI, and the diagnostic scaffolding removed `[VERIFIED by CI]`

Commit `edc0048` changes exactly two things in `src/geode/BenchmarkUI.hpp`: the button map
becomes `std::map<Btn, ::CCMenuItemSpriteExtra*>`, and `bool init()` gains `override`.
Nothing else in the repository was touched to obtain a passing build - no target removed,
no source excluded, no warning suppressed, no `mod.json` or `CMakeLists.txt` change.

Workflow `Build & Verify OptiGD`, run `38108365707` (push event) and run `38108363696`
(pull-request event), both on `edc0048`: **all 6 jobs passed.**

| Job | Job ID (`38108365707`) | Result |
| --- | --- | --- |
| Core Engine Tests & Benchmarks (ubuntu-latest) | `114378570355` | success |
| Core Engine Tests & Benchmarks (windows-latest) | `114378570482` | success |
| Geode Mod (Windows x64) | `114378862760` | success |
| Geode Mod (Android64 (arm64-v8a)) | `114378862767` | success |
| Geode Mod (Android32 (armeabi-v7a)) | `114378862722` | success |
| Package Combined .geode Artifact | `114379175513` | success |

The per-platform verification steps ran and passed, so the archives themselves were
inspected rather than assumed: each platform's `.geode` was listed with `unzip -l` and
grepped for its own binary name, and `mod.json` inside each archive was asserted with
`jq -e` to carry `.version == "v0.1.0-beta.3"` and `.id == "coderunknow.optigd"`. The
combined archive (artifact `coderunknow.optigd-geode-package`, **1,037,855 bytes**, against
470,639 bytes for beta.2) was asserted to contain all three binaries plus the same
`mod.json` version. The linking and packaging stages that run 1 had never reached were
therefore exercised for the first time in this release, and they produced the artefacts
criterion 10 asks about.

`.github/workflows/geode-diagnose.yml` and `.github/geode-diagnose-trigger` are deleted in
the commit that records this section. They existed for one purpose - to make an
unreachable compiler error readable - and keeping a workflow whose only job is to post raw
build logs as pull-request comments would leave a noisy, unpinned surface in the
repository. If a future Geode-only compile error needs the same treatment, the recipe is
in §6.12: copy the composite action's own configure/build commands, redirect them to a
file, and post the filtered result somewhere the API can read. Its last run
(`38108363653`) failed in the log-extraction step, after a successful configure and build,
because the extraction script assumed there would be errors to find; that is scaffolding
behaviour, not a finding about OptiGD, and the real workflow's jobs are the authority.

Two honest boundaries on this green run. First, it verifies compilation, linking and
packaging on three targets - it does not verify that the mod loads, that the pause-menu
button appears, that the popup renders, or that a capture records anything, because no
Geometry Dash client, Geode loader, GPU or Android device took part. Second, the bindings
the Geode jobs compile against are whatever `geode-sdk/bindings` `main` happens to be when
the job runs; that is a property of the upstream action, and it means a green run is a
statement about a moment, not a permanent guarantee.

---

## Entry 7 — 2026-10-12: Real-game handoff pass — the path from the pause menu to a measured capture, and an installable artefact

Entry 6 ended with beta.3 compiling, linking and packaging on three targets, and with
every criterion that this environment can decide decided. What it could not produce was
the one artefact a maintainer needs before merging: a `.geode` file they can put in their
mods folder and play. This entry is that pass. It re-audited the code along the only path
that matters in the real game — *pause menu → start a capture → get back to playing →
finish → read the record* — found five defects on it, fixed them, added a regression guard
for the class of bug that caused the worst one, and taught CI to publish a named,
checksummed, per-platform package.

### 7.1 The request, condensed

Fix what demonstrably affects real-game use of Benchmark Mode (entry point, popup, HUD,
the transition back into active gameplay, the lifecycle paths, configuration restoration,
session integrity and truthful classification) using the existing architecture; produce a
downloadable Windows x64 `.geode` for GD `2.2081` + Geode `5.10.1` built from the exact
final commit, installable through the normal Geode workflow, clearly identified, with its
SHA-256 recorded, and without committing binaries or cutting a release; verify everything
verifiable and report the rest as `[NOT RUN]` / `[BLOCKED]` with the real reason.

### 7.2 What the audit found `[VERIFIED by source read against geode-sdk v5.10.1 and geode-sdk/bindings main]`

Continuing the defect lettering from §6.3 and §6.7.

| # | Finding | Where | Why it matters / what was done |
|---|---|---|---|
| W | `benchmark-duration-seconds` is declared `"type": "int"` but was read as `double` | `src/geode/BenchmarkUI.cpp`, `applyBenchmarkSettingsToPolicy()` | `Mod::getSettingValue<T>` maps `T` onto a *setting type* and RTTI-casts the stored setting; a mismatch returns `T()` with **no error and no log line** (§7.3). The duration setting was therefore inert: whatever the user chose, every capture ran at the controller's built-in default, and the panel reported a target the user never asked for. Now read as `int64_t`, with a `log::warn` if the read yields `< 1` and the default is kept, so the failure is visible in the Geode log instead of silent. |
| X | The Geode layer clamped the wall-clock ceiling to `target / 0.9` | `src/geode/BenchmarkUI.cpp`; rule now in `src/benchmark/BenchmarkController.hpp` | A ceiling with ~11 % slack is not a guard, it is a budget: the first pause, death or loading stall longer than the slack would finalize an otherwise healthy capture as `incomplete`. The rule moved into the core as `kWallClockCeilingFactor = 15.0` with `recommendedWallClockCeilingMs()`; `setPolicy()` keeps its existing behaviour (repair a ceiling *below* the target) and the Geode helper now **widens only, never narrows**. |
| Y | Frames after the level ended were counted as gameplay | `src/geode/Hooks.cpp`, `GJBaseGameLayer::update` gate | `update` keeps running through the end animation, the completion effects and the results screen. Those frames are cheap to render, so a successful run — the session a player is most likely to keep open — inflated `avg_fps` and pulled every percentile down. The gate now also requires `!m_hasCompletedLevel && !m_levelEndAnimationStarted` (§7.5). |
| Z | The benchmark HUD label was anchored top-right | `src/geode/Hooks.cpp`, `PlayLayer` fields | GD's standard play-layer layout puts the pause button top-right; the label is several lines tall at `zOrder 9999`, so it drew over the one control a capture needs. Moved to bottom-right (anchor `{1,0}`), which the diagnostics overlay (top-left) and the progress bar (top-centre) leave free. The label is not in a menu and never swallows touches — this is about not hiding controls, not about input. |
| UX | The running-state panel text never said how to make the number move | `src/ui/BenchmarkPanel.cpp`, `formatMain` | A capture is normally started from the pause menu, where the window cannot advance. Without saying so, the panel reads as a countdown hung at `0.00 s`. The running branch now states: close the panel and **resume** the level; the window advances only during active gameplay; pauses, alt-tabs and stalls over 250 ms are excluded and reported separately. |

Checked and deliberately left alone, because the audit found them correct:

- `onQuit` and `PlayLayer::init` forward to `OptiGDManager::onLevelExit()`, which finalizes
  an open session as `incomplete` with `level_exit` **and** restores the user's
  configuration. Leaving a level mid-capture cannot strand the override.
- `resetLevel` forwards to `onLevelReset()`, which honours `benchmark-stop-on-death`
  (`stopOnLevelReset`): stop and finalize, or keep measuring with `reset_events` counted and
  a `segment_not_contiguous` warning.
- `startSession` has no "must be playing" precondition, which is what makes the pause-menu
  entry point work at all. The window simply does not advance until play resumes.
- The ceiling is enforced in `onFrame`, i.e. only while the game loop runs — a session left
  open in a paused game does not silently expire.
- Every terminal path (`finalize`, cancel, level exit, level reset, ceiling breach, persist
  failure) clears the transient override before persisting, and `finalize` is idempotent.

### 7.3 Root cause of W, verified in the SDK rather than assumed

`geode-sdk` v5.10.1, `loader/include/Geode/loader/Mod.hpp`:

```cpp
template <class T> T getSettingValue(std::string_view key) const {
    using S = typename SettingTypeForValueType<T>::SettingType;
    if (auto setting = getSetting(key); setting &&
        setting->getTypeInfo() == cast::typeinfo_pointer_cast<S>(...)) { ... }
    return T();   // <- mismatch: silent default, no log, no error
}
```

`SettingV3.hpp` fixes the mapping: `bool` → `BoolSettingV3`; **any** `std::integral` →
`IntSettingV3`; **any** `std::floating_point` → `FloatSettingV3`; `std::string` /
`string_view` → `StringSettingV3`; `std::filesystem::path` → `FileSettingV3`; `ccColor3B` /
`ccColor4B` and the `Keybind` containers → their own types. `listenForSettingChanges<T>`
uses the same mapping, so a mismatched listener does not misfire — it never fires at all.

The rule this leaves behind, and the reason §7.6 exists: **a `mod.json` `"type": "int"`
setting must be read through an integral C++ type, and a `"type": "float"` setting through a
floating-point one.** There is no conversion, only a cast, and the cast failing is
indistinguishable from the user having chosen `0`.

### 7.4 The ceiling is a guard, not a budget

Two numbers, so the semantics are unambiguous. `targetDurationMs` is what the capture is
*for*: accumulated **active gameplay** time (§6.8). `maxWallClockMs` is how long the session
may stay open in wall-clock terms before it is treated as a runaway. Active time excludes
pauses, stalls, the level intro and (since Y) the level-end animation, so wall time is
always strictly larger — by an amount that depends entirely on how the player plays. A
ceiling tight enough that ordinary play breaches it converts "the player paused once" into
"a failed measurement".

At `15 ×`, a 60 s target may stay open for 15 minutes of wall time. The session that
breaches that is genuinely abandoned, and it is finalized as `incomplete` with
`wall_clock_exceeds_captured_window` rather than left running forever. The factor is now a
named core constant used by both the core's own repair path and the Geode helper, so the two
can no longer disagree.

### 7.5 What the level-end gate excludes, and what it deliberately does not

The gate reads two plain data members: `PlayLayer::m_hasCompletedLevel` and
`GJBaseGameLayer::m_levelEndAnimationStarted` (ownership confirmed in `GeometryDash.bro`,
lines 15202 and 8167). Their conjunction is what GD's own `PlayLayer::canPauseGame()`
computes — but that reconstruction is declared `win inline` only (with addresses for imac,
m1 and ios and **none** for Android), so calling it from cross-platform code would not link
on Android32/Android64. Reading the members is portable and is the same information.

Still measured on purpose: the attempt's own intro frames and the death animation. The level
is being played during those, they cost real frame time, and they are symmetric between a
baseline and an optimized capture — excluding them would bias nothing and hide a real cost.
The level *end* is different: it is not gameplay, it is cheap, and it only happens in the
sessions a player keeps.

### 7.6 Regression guard: `tools/audit_setting_types.py`

Defect W is the kind of bug that no amount of unit testing catches, because the C++ under
test is correct in isolation — the defect is a *disagreement between two files*, and the
failure mode is silence. So the guard is out of process, like the other validators: it
parses `mod.json`, scans `src/` for `getSettingValue<T>("key")` and
`listenForSettingChanges<T>("key", …)` (comments and string literals stripped, template
argument lists read with a depth counter so `std::vector<geode::Keybind>` works), maps `T`
through the SDK's own table and reports any disagreement. It also reports a key that is not
declared, a call site with no template argument (which cannot be verified), and a C++ type
outside the mapping (reported rather than guessed).

It is mutation-tested two ways. Its `--self-test` runs 13 synthetic fixtures — the whole
mapping agreeing, the beta.3 `int`-as-`double` defect, `bool`-as-`string`, `float`-as-`int`,
a mismatched listener, an undeclared key, an unverifiable call site, an unmappable type,
comment/string mentions that must not count, a declared-but-unread setting (a note, not an
error), an unknown declared type, qualified/`const` spellings, and an empty tree. And it was
run against the real tree with W reintroduced: it exits `1` naming
`src/geode/BenchmarkUI.cpp:209`, and exits `0` with the fix in place. Registered in ctest as
`OptiGDSettingTypeAudit` (the real audit) and `OptiGDSettingTypeAuditSelfTest` (the
guard's own guard) — `ctest` grows from 6 to **8** checks.

### 7.7 Verification actually run this pass `[MEASURED]`

| Check | Command | Result |
|---|---|---|
| Unit tests (Release, standalone) | `./build/optigd_tests` | **51 passed / 0 failed** (was 50) |
| Project checks | `ctest --test-dir build` | **8/8 passed** |
| ASan + UBSan, `-fno-sanitize-recover=all` | `./build-asan/optigd_tests` + `ctest --test-dir build-asan` + `./build-asan/optigd_benchmark` | **51/51**, **8/8**, no sanitizer diagnostics, clean exit |
| Setting-type audit vs. real tree | `tools/audit_setting_types.py` | 16 declared settings, 32 call sites, **PASS** |
| Auditor mutation test | `tools/audit_setting_types.py --self-test` | **13/13 cases PASS**; plus the real-tree mutation above (`FAIL` with W reintroduced) |
| Workflow YAML | `yaml.safe_load` over `.github/workflows/build.yml` | parses; both new steps present in their jobs |
| Packaging step, dry-run | extracted `run:` script executed locally against a stand-in `.geode` | exit `0`; produced the named file, `SHA256SUMS.txt`, `INSTALL.txt`, the step-summary table and the four `$GITHUB_OUTPUT` values |

The new test is `testBenchmarkWallClockCeilingIsAGuardNotABudget`. It sets a 5 s target with
a 6 s ceiling, drives the controller through the ceiling with **zero** accepted frames and
asserts the session ends `invalid` and non-comparable with `wall_clock_exceeds_captured_window`,
`paused` and `no_samples_recorded`, that it was persisted, that the override was cleared and
that the user's configuration came back byte-for-byte; then it runs a second session with 40
real frames, pauses, breaches the ceiling and asserts `incomplete` with
`wall_clock_exceeds_captured_window` and `capture_shorter_than_target`, a **distinct** session
id, and the same restoration. `testBenchmarkPanelTextAndPagination` now asserts the resume
guidance is in the running-state text.

Two traps this test hit, recorded so the next one does not: `setPolicy()` silently repairs a
`maxWallClockMs` below `targetDurationMs`, so a ceiling-breach test must use a ceiling *at or
above* the target (an "unusable" ceiling is repaired away and never breaches); and
`finalize()` derives the terminal state from the data, so a breach with no samples is
`invalid`, not `incomplete`.

Not run, and why: no Geometry Dash `2.2081` binary, no Geode loader, no GPU or display, no
Windows host and no Android device exist in this environment, so nothing in `src/geode/*` can
be executed here — it is compiled only by CI. The `Hooks.cpp` and `BenchmarkUI.cpp` changes
above are therefore **compile-unverified locally** at the moment they were written; CI is the
first thing that builds them.

### 7.8 The installable artefact

CI already uploaded `coderunknow.optigd-geode-package` (all three platforms in one archive)
plus the two standalone JSON artefacts. What it did not upload was anything a tester could
identify: no platform in the name, no version, no commit, no checksum. `build.yml` now adds,
per Geode target, a second artefact named `OptiGD-<version>-<target>-<short-sha>` containing
`coderunknow.optigd-<version>-<target>.geode`, `SHA256SUMS.txt` and an `INSTALL.txt` written
for that platform, and the same treatment for the combined archive as
`OptiGD-<version>-combined-<short-sha>`. The Windows x64 one is the deliverable this task
asked for.

Each of those steps also writes the version, target, commit, run id, byte size, SHA-256 and
the `unzip -l` archive listing into `$GITHUB_STEP_SUMMARY`, so the checksum is readable from
the run page and from the API (`/check-runs/{id}` → `.output.summary`) without downloading
anything. The existing artefact names are untouched — `release.yml` depends on
`coderunknow.optigd-geode-package` — and nothing is committed to the repository: binaries
stay in Actions storage. `if-no-files-found: error` makes a silently empty upload a failed
job rather than a missing artefact.

### 7.9 What remains unverified, precisely

- **Nothing in this repository has ever been loaded by Geode.** A green Geode job means
  *compiles, links, packages, and the archive contains the expected binary and metadata*.
- **The HUD's new bottom-right placement and the pause-button overlap it avoids are
  judgements from GD's UI convention and the bindings, not from a rendered frame.**
  `inline/PauseLayer.cpp` upstream reconstructs only `createToggleButton` — there is no
  source for the pause-menu layout to check against — so the button and popup geometry are
  likewise unverified visually. These are items 2 and 7 of the smoke test handed to the
  maintainer.
- **The pause → resume transition has never been observed in the real game.** It is now
  stated in the panel text and the session semantics are tested headlessly with a synthetic
  clock, but "the window advances when the player resumes" is a claim about GD's loop that
  only a live run can confirm.
- **Live frame-time captures, FPS effects and the CapFrameX procedure remain
  `[NOT RUN] / [BLOCKED]`**, as in §6.10 and §5 of `EVIDENCE_REPORT.md`.
- The bindings the Geode jobs compile against are `geode-sdk/bindings` `main` at run time —
  a property of the upstream composite action, unchanged by this pass.

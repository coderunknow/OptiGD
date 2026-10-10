# OptiGD `v0.1.0-beta.2` Engineering Verification & Evidence Report

**Target Release**: `v0.1.0-beta.2` (Release Candidate)
**Target Game / Loader**: Geometry Dash `2.2081` / Geode SDK `v5.10.1`
**Target Platforms**: Windows x64 (`Win64`), Android 64-bit (`Android64` / `arm64-v8a`), Android 32-bit (`Android32` / `armeabi-v7a`)
**Supersedes**: the `v0.1.0-beta1` report (kept in git history; `v0.1.0-beta1` never loaded, so none of its runtime claims could ever be exercised)

---

## 1. Evidence Classification Taxonomy

To ensure complete transparency and zero fabricated claims, every subsystem and metric in OptiGD is classified using four strict tags:

- **`[VERIFIED]`**: Verified by automated unit/integration tests (`ctest`) and/or multi-platform Geode SDK compilation (`Win64`, `Android32`, `Android64`).
- **`[MEASURED]`**: A number produced by an instrumented run in this environment (test suite, benchmark binary, or validator), reproducible with the commands given.
- **`[PARTIAL]`**: Core algorithm and unit tests are verified and hooks are wired safely, while live in-game Geometry Dash runtime validation remains pending.
- **`[SIMULATED]`**: Measured in the deterministic standalone C++20 stress benchmark suite (`optigd_benchmark`), not inside a live Geometry Dash `2.2081` process.
- **`[UNVERIFIED / BLOCKED]`**: Requires a runnable Geometry Dash `2.2081` + Geode `v5.10.1` graphical runtime environment, which is unavailable in headless CI/sandbox containers.
- **`[NOT RUN]`**: The check exists but did not execute in this environment. Stated explicitly rather than reported as a pass.

---

## 2. Root Cause Analysis (RCA)

### 2.1 Symptom

With `v0.1.0-beta1`, Geometry Dash started normally but OptiGD was absent from the mod list and produced no settings. The Geode log contained a load problem reported as:

```
unexpected type string
```

### 2.2 Root cause `[VERIFIED]`

`mod.json` declared `"version": "v0.1.0-beta1"`. That string is **not** a valid Geode `VersionInfo`, so metadata deserialization fails before any OptiGD code runs.

Evidence chain, read from the unzipped Geode SDK `v5.10.1` source (`/tmp/geode-5.10.1`):

1. `loader/src/loader/ModMetadataImpl.cpp:288` — `root.needs("version").into(impl->m_version)`, where `m_version` is a `geode::VersionInfo`.
2. `matjson::Serialize<VersionInfo>::fromJson` delegates to `VersionInfo::parse` (`loader/src/utils/VersionInfo.cpp`).
3. `VersionInfo::parse` consumes `major.minor.patch`, then hands the remainder to `VersionTag::parse`. `VersionTag::parse` accepts the literals `alpha`, `beta`, `rc`, `prerelease`, etc., each optionally followed by `.` + a number. With `-beta1` it matches `beta` and then leaves `'1'` unconsumed.
4. `VersionInfo::parse` therefore returns `Err("Expected end of version, found '1'")`.
5. `loader/include/Geode/utils/JsonValidation.hpp:105-118` — `tryGet<T>()` flattens any member deserialization error into the generic message **`unexpected type string`**, which is why the log never mentioned the version.
6. `loader/src/loader/LoaderImpl.cpp:626-630` — the failed validation becomes `addProblem({InvalidGeodeFile})` and the mod is skipped.

### 2.3 Dynamic confirmation `[MEASURED]`

The grammar was extracted verbatim from `v5.10.1` into a standalone harness (`/tmp/verify_version/`, outside the repository) and executed:

| Input | Result |
|---|---|
| `v0.1.0-beta1` | **ERR** — `Expected end of version, found '1'` |
| `v0.1.0-beta2` | **ERR** — `Expected end of version, found '2'` |
| `v0.1.0-beta.2` | **OK** — `0.1.0`, tag `Beta`, tag number `2` |
| `v0.1.0-beta.1` | **OK** |
| `v0.1.0-beta` | **OK** |
| `v0.1.0` | **OK** |
| `5.10.1` / `v5.10.1` | **OK** |
| `v0.1.0-alpha.3` | **OK** |
| `2.2081` | **ERR** — `Patch version missing` |

This also disproves the alternative hypothesis that the `tier-preset` setting (a `string` with a `one-of` list) caused the failure: `loader/src/loader/SettingV3.cpp:980-1019` explicitly supports `string` + `one-of`.

### 2.4 Fix and why this exact string

`mod.json` now declares `"version": "v0.1.0-beta.2"`.

`v0.1.0-beta2` — the obvious "beta 2" spelling — **reproduces the original failure** (`Expected end of version, found '2'`). `v0.1.0-beta.2` is the only prerelease form Geode `5.10.1` accepts that still preserves the `beta2` designation.

### 2.5 Regression guards

| Guard | What it prevents |
|---|---|
| `src/core/GeodeVersion.{hpp,cpp}` | A faithful port of `VersionInfo::parse` / `VersionTag::parse`, so the exact grammar is exercised without the SDK. |
| `tests/UnitTests.cpp::testGeodeVersionMetadataGrammar` | Asserts `v0.1.0-beta1` and `v0.1.0-beta2` are rejected, `v0.1.0-beta.2` parses to `0.1.0` + `Beta` + `2`, and round-trips through `toVString()`. |
| `tools/validate_geode_metadata.py` (ctest `OptiGDGeodeMetadataValidation`) | Validates `mod.json` against the SDK's mod-id regex, `gd` object shape, `VersionInfo` grammar, and settings rules — and cross-checks `version` / `geode` / `gd` against `Types.hpp` and `CMakeLists.txt` so the four copies cannot drift apart again. |
| `OptiGDManager::initialize()` self-check | Logs `ERROR` at startup if `kOptiGDVersionTag` fails `isValidGeodeVersionString`. |
| `.github/workflows/{build,release}.yml` | `jq -e '.optigd_version == "0.1.0-beta.2"'` and `jq -e '.version == "v0.1.0-beta.2"'`. |

### 2.6 Evidence separation (per the standing requirement)

- **Code-level `[VERIFIED]`**: the parse chain above, plus the extracted-grammar harness result.
- **Packaging `[VERIFIED]`**: `mod.json` now passes `tools/validate_geode_metadata.py`, and the CI packaging job asserts the shipped archive's `mod.json` carries `v0.1.0-beta.2`.
- **In-game `[UNVERIFIED / NOT RUN]`**: this environment has no Geometry Dash `2.2081` binary, no GPU context, and no Geode loader, so "the mod now appears in the Geode mod list and can be enabled" has **not** been observed. That confirmation requires a hardware tester run and remains open.

---

## 3. Module-by-Module Evidence Table

| Module | Status | Runtime Integration Point | Safe Default & Fallback Behavior | Automated Tests (`UnitTests.cpp`) | Platform Coverage | Supporting Evidence |
|---|---|---|---|---|---|---|
| **`MemoryArena`** (`src/core/MemoryArena.*`) | `VERIFIED` | `OptiGDManager::beginFrame()` resets arena; used per frame by `VisibilityCuller::evaluateVisibilityDelta()` (`SpatialHash::queryIDsArena`) and `AdaptiveGovernor::calculateWindowStats()`. | Power-of-two alignment normalization; 0-byte and overflow allocation guards return `nullptr`; auto-expands blocks on demand and reuses blocks across resets. | `testMemoryArena`: alignment (`1..64` + non-pow2), 0-byte guard, overflow rejection, block expansion & reuse. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` `[MEASURED]` 19/19 pass; also clean under ASan+UBSan. Fixed in beta.2: block reuse no longer destroys live frame-scoped data. |
| **`SpatialHash`** (`src/core/SpatialHash.*`) | `VERIFIED` | Backing spatial index for `VisibilityCuller`; updated on `PlayLayer::addObject` and `GJBaseGameLayer::updateObjectSection`. | `Rect2D::normalized()` clamps non-finite/negative dimensions; `kMaxCellSpan = 64` caps pathological bounds; incremental cell re-indexing on `update()` and `remove()`. | `testSpatialHash`: insert, moving-object `update()` cell re-indexing, negative coordinates, `remove()`, `queryArena()`. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass; verified zero stale bucket references after object movement. |
| **`OptiGDManager`** (`src/core/OptiGDManager.*`) | `VERIFIED` | Initialized in `$on_mod(Loaded)` with live `Loader::get()->getVersion().toVString()`; frame lifecycle driven by `GJBaseGameLayer::update(dt)`. | Separates `totalFrameTimeUs` from `updateCpuTimeUs` and `optigdOverheadUs`; propagates fallback/tier changes to `ConfigManager`; startup self-check on the version tag. | `testManagerAndCompatibility`: full lifecycle, version check (`0.1.0-beta.2`, `v5.10.1`), arena & avoided-heap accounting, per-subsystem timing accessors. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + cross-platform Geode hook compilation. |
| **`VisibilityCuller`** (`src/optimization/VisibilityCuller.*`) | `PARTIAL` | Hooked in `PlayLayer::addObject`, `PlayLayer::updateVisibility(dt)`, `PlayLayer::resetLevel()`, and `GJBaseGameLayer::updateObjectSection(GameObject*)`. | Runs *after* native `PlayLayer::updateVisibility(dt)`; only toggles visibility on decorative, non-trigger objects (`m_isDecoration \|\| m_isDecoration2`); respects `m_isHide`, `m_isInvisible`, `m_isDisabled`. Disabling culling mid-level now calls `restoreAllVisibility()` so hidden decorations are restored. | `testVisibilityCuller` + `testVisibilityCullerCellSizeReindex`: frustum culling, pointer-based bounds update, differential `evaluateVisibilityDelta`, `setCellSize()` mid-level re-index stability, `restoreAllVisibility()` / `hasCulledObjects()` round-trip. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + Geode `Win64`/`Android32`/`Android64` build; live GD visual check `[UNVERIFIED]`. |
| **`ParticleGovernor`** (`src/optimization/ParticleGovernor.*`) | `PARTIAL` | Hooked in `cocos2d::CCParticleSystem::update(float dt)` and `PlayLayer::addObject`. | Never sleeps `Critical` emitters (player trails) or finite-duration / `autoRemoveOnFinish` emitters; stores `baseEmissionRate` to prevent compounding decay; clamps `dt` and resets `m_fEmitCounter` on wake. | `testParticleGovernor` + `testParticleGovernorIncrementalTotals`: critical/finite-duration preservation, 50-frame non-compounding rate check, wake `dt` clamp, O(1) incremental totals across register/shrink/unregister. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + Geode build; live GD visual check `[UNVERIFIED]`. |
| **`TriggerOptimizer`** (`src/optimization/TriggerOptimizer.*`) | `PARTIAL` | Hooked in `PlayLayer::addObject`, `PlayLayer::setupHasCompleted`, `PlayLayer::postUpdate`, `PlayLayer::resetLevel`, `GJBaseGameLayer::spawnGroup`, `GJBaseGameLayer::toggleGroup`. | **Disabled by default** (`enable-trigger-indexing = false`). When enabled it only inspects: `inspectLookaheadWindow()` no longer mutates `activated`/`isCompleted`, so telemetry cannot change what native GD executes. Counters report `0` when indexing is off. | `testTriggerOptimizer`: `TouchOrPosition`, `TouchOnly` hitbox intersection, `isMultiTriggered`, deterministic `(x, spawnOrder, id)` sort, group toggle, backward cursor rewind, spawn-group dispatch. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + Geode build. Trigger counters are *not* an execution claim. |
| **`MatrixCache`** (`src/optimization/MatrixCache.*`) | `PARTIAL` | Used in `extractGameObjectBounds` (`PlayLayer::addObject`, `GJBaseGameLayer::updateObjectSection`) and evicted in `GJBaseGameLayer::removeObjectFromSection`. | `markDirty()` invalidates node and recursively invalidates children; `getCached()` rejects dirty **and never-computed** entries; `evict()` detaches parents/children; `pruneStaleEntries()` drops phantom nodes; `store()` is anchor-aware. | `testMatrixCache` + `testMatrixCacheStaleEntryPruning`: hit/miss, dirty invalidation, parent-child propagation, eviction, `AffineMatrix` equivalence, phantom-entry rejection, anchor keying, pruning. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + Geode build. |
| **`AdaptiveGovernor`** (`src/adaptive/AdaptiveGovernor.*`) | `VERIFIED` | Executed at end of each frame in `OptiGDManager::endFrame()`. | Rolling 120-frame mean, median (P50), P95, P99, std-dev using `MemoryArena` scratch; 120-frame hysteresis cooldown; locked when tier is `Custom`. Tier transitions apply `core::kTierDefaults` — the same table `ConfigManager` presets use. | `testAdaptiveGovernorAndFallback` + `testTierDefaultsSingleSource`: rolling window percentiles, automatic tier downgrade under budget pressure, arena usage, and agreement between automatic transitions and hand-picked presets. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass. |
| **`FallbackManager`** (`src/adaptive/FallbackManager.*`) | `VERIFIED` | Executed in `OptiGDManager::endFrame()` via `checkAndApplyFeatureFallbacks()`. | Detects frame-time jitter (`stdDevMs > threshold`) or per-feature CPU overruns, snapshots pre-fallback config, downgrades safely, restores after stable recovery frames. **Recovery is now evaluated every frame; the cooldown gates only new fallbacks.** | `testAdaptiveGovernorAndFallback` + `testFallbackRecoveryDuringCooldown`: trigger, downgrade, and recovery — including recovery while a cooldown is pending. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass. |
| **`Profiler`** (`src/profiling/Profiler.*`) | `VERIFIED` | Records per-subsystem durations in `OptiGDManager::endFrame()` and scoped timing via `OPTIGD_PROFILE_SCOPE`. | Hot path uses `recordById(MetricId, …)` — a fixed `std::array` slot, no mutex, no allocation. Ad-hoc names fall back to a mutex-guarded overflow map. | `testManagerAndCompatibility` + `testProfilerFastPath`: id/name paths share accumulators, min/max/mean correctness, `reset()` clears both. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + clean build. |
| **`DiagnosticsEngine`** (`src/diagnostics/DiagnosticsEngine.*`) | `VERIFIED` | Records per-frame stats in `OptiGDManager::endFrame()`; exports `optigd_diagnostics.json` on `PlayLayer::onQuit()`. | 100% local JSON telemetry (zero network). All interpolated strings are now escaped. Emits a `notes` block declaring `native_trigger_execution_modified: false` and defining `total_triggers_skipped`. | `testDiagnosticsJsonEscaping` + ctest `OptiGDDiagnosticsJson` (real artefact, schema-validated out of process). | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + `tools/validate_diagnostics_json.py`. |
| **`DiagnosticsOverlay`** (`src/ui/DiagnosticsOverlay.*`) | `PARTIAL` | Rendered via `CCLabelBMFont` (`chatFont.fnt`) in `PlayLayer::init` and updated every 10 frames in `PlayLayer::postUpdate`. | Disabled by default (`enable-diagnostics-overlay = false`); displays `[OptiGD v0.1.0-beta.2 Diagnostics]`, FPS, Mean/P50/P95/P99, Update CPU, OptiGD Overhead, Culling delta, Particles, Triggers, Arena stats. | `testManagerAndCompatibility`: formatted HUD output string and version header. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + Geode build. |
| **`CompatibilityLayer`** (`src/compatibility/CompatibilityLayer.*`) | `VERIFIED` | Initialized in `OptiGDManager::initialize(gdVer, geodeVer)` and checked before executing hooks. | GD gate is now an exact numeric comparison against `kTargetGDVersion` (`2.2081`), replacing a `starts_with("2.2")` prefix test that would have accepted GD `2.21x`. Per-feature conflict isolation without crashing. | `testManagerAndCompatibility` + `testGdVersionCompatibilityGate`: `2.2081`/`v2.2081`/`*` accepted; `2.21`, `2.2074`, `3.0`, `""`, `banana` rejected. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass. |
| **`ConfigManager`** (`src/config/ConfigManager.*`) | `VERIFIED` | Synchronized in `Hooks.cpp` (`syncSettingsWithConfig()`) via `ConfigManager::syncFromSettings()`. | Applies the tier preset first, then `Custom` parameters and feature flags without stale-copy overwrite; `deserialize` no longer throws on malformed persisted text. | `testConfigAndPresetSync` + `testConfigDeserializeRobustness`: all 4 presets, file/string round-trip, malformed-line skipping. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass. |
| **`GeodeVersion`** (`src/core/GeodeVersion.*`) | `VERIFIED` | New in beta.2. Used by the startup self-check and the metadata validator. | Pure function, no allocation of state, no dependencies. | `testGeodeVersionMetadataGrammar`. | Standalone (`Linux`, `Win64`) | `ctest` pass + cross-checked against the extracted SDK harness. |

---

## 4. Synthetic Stress Benchmark Evidence `[SIMULATED]`

Captured by `./build/optigd_benchmark` in this environment (Linux x86_64, GCC 12.2.0, `-O3`, fixed deterministic seed `42`, **median of 5 runs**):

| Scenario | Objects | Emitters | Triggers | Baseline Mean | OptiGD Mean | Baseline P99 | OptiGD P99 | Speedup | CPU Reduction |
|---|---|---|---|---|---|---|---|---|---|
| 1. Standard Rated Level (Medium Density) | 4,000 | 40 | 600 | 53.43 us | 9.58 us | 89.88 us | 49.22 us | **5.45x** | **81.64%** |
| 2. High-Object Extreme Demon (25k Objects) | 25,000 | 180 | 4,000 | 322.54 us | 60.07 us | 442.00 us | 201.79 us | **5.36x** | **81.34%** |
| 3. Ultra-Dense Mega-Collab Stress (60k Objects) | 60,000 | 350 | 10,000 | 724.13 us | 175.55 us | 995.48 us | 502.00 us | **4.19x** | **76.15%** |

| Scenario | Frames | Baseline Heap Allocations | OptiGD Per-Frame Heap Allocations | OptiGD Arena Bump Allocations |
|---|---|---|---|---|
| 1. Standard Rated Level | 180 | 1,785 | **0** | 360 |
| 2. High-Object Extreme Demon | 180 | 2,292 | **0** | 360 |
| 3. Ultra-Dense Mega-Collab Stress | 120 | 1,680 | **0** | 240 |

*Note*: These `[SIMULATED]` benchmarks isolate the algorithmic complexity of `SpatialHash` differential frustum culling, `ParticleGovernor` offscreen sleeping, `TriggerOptimizer` lookahead indexing, `MatrixCache` lookup, and `MemoryArena` bump allocation against naive $O(N)$ linear scans. Absolute microsecond values move with host load on a shared 2-core container; the **reproducible invariants** are the speedup factor and the zero per-frame heap allocations. **They are not Geometry Dash frame times and imply no live-game frame-rate gain.**

---

## 5. Real-Game Geometry Dash Runtime Benchmarks `[UNVERIFIED / BLOCKED]`

### Status: `BLOCKED` by Headless Environment

- **Why Blocked**: Executing real-game benchmarks requires a licensed Geometry Dash `2.2081` binary, an active OpenGL/GLES GPU context, and the Geode `v5.10.1` runtime loader on Windows x64 or Android. None of these exist inside the headless Linux build/CI container.
- **No substitute is claimed.** Every `[SIMULATED]` number above is a standalone harness result; none of it is a proxy for in-game frame time.

### Built-In Telemetry Harness for Hardware Testers

1. Install `coderunknow.optigd.geode` into Geometry Dash `2.2081` with Geode `v5.10.1`.
2. **Baseline Capture**: In Mod Settings, disable `Spatial Frustum Culling`, `Particle System Governor`, `Adaptive Frame-Time Governor`, and `Transform Matrix Caching` (passthrough telemetry mode). Play a target level and exit; copy `<GD Save Dir>/geode/mods/coderunknow.optigd/optigd_diagnostics.json` to `baseline_level.json`.
3. **OptiGD Capture**: Re-enable the optimization settings (`LowEnd` or `Balanced` preset), play the same level over the same segment, and copy `optigd_diagnostics.json` to `optigd_level.json`.
4. **Generate Comparative Report**:
   ```bash
   python3 tools/generate_report.py --compare baseline_level.json optigd_level.json
   ```
   This outputs a Markdown comparison table with exact before/after deltas for Average FPS, Mean/Median(P50)/P95/P99 frame times, Mean Update CPU time, Culled Objects, Avoided Particles, and Avoided Heap Allocations.

---

## 6. Automated Test & Validation Evidence `[MEASURED]`

All executed in this environment on the standalone engine build (Linux x86_64, GCC 12.2.0, `-O3`).

| Check | Command | Result |
|---|---|---|
| Unit & integration suite | `ctest -R OptiGDUnitTests` | **PASS** — 20/20, exit 0 |
| Diagnostics artefact schema | `ctest -R OptiGDDiagnosticsJson` | **PASS** — real report emitted, 0 schema errors |
| `mod.json` metadata | `ctest -R OptiGDGeodeMetadataValidation` | **PASS** — 0 errors |
| Benchmark artefact schema | `ctest -R OptiGDBenchmarkJsonSchema` | **PASS** — 3 scenarios, `[SIMULATED]`, 0 heap allocs |
| Full suite | `ctest --output-on-failure` | **PASS** — 4/4 |
| Address + UB sanitizers | RelWithDebInfo + `-fsanitize=address,undefined`, full suite and both binaries | **PASS** — no ASan/UBSan findings, no leaks |

Test inventory (19): `testMemoryArena`, `testSpatialHash`, `testVisibilityCuller`, `testParticleGovernor`, `testTriggerOptimizer`, `testMatrixCache`, `testAdaptiveGovernorAndFallback`, `testConfigAndPresetSync`, `testManagerAndCompatibility`, plus the eleven added for beta.2 — `testGeodeVersionMetadataGrammar`, `testVisibilityCullerCellSizeReindex`, `testParticleGovernorIncrementalTotals`, `testDiagnosticsJsonEscaping`, `testConfigDeserializeRobustness`, `testCameraWorldRectConvention`, `testMatrixCacheStaleEntryPruning`, `testFallbackRecoveryDuringCooldown`, `testProfilerFastPath`, `testGdVersionCompatibilityGate`, `testTierDefaultsSingleSource`.

### Camera convention note `[VERIFIED by binding inspection]`

`GJBaseGameLayer::m_gameState.m_cameraPosition` is the **bottom-left** of the visible area in object-layer units, not its centre; the view spans `[camX, camX + winSize.width / zoom]`. Evidence from the pinned GD `2.2081` bindings:

- `inline/PlayLayer.cpp screenFlipObject()` mirrors `x` about `camX + (winSize.width / zoom) / 2`.
- `inline/GJBaseGameLayer.cpp lightningFlash()` uses `m_cameraPosition.y + (winSize.height + 50) / scale` for the top edge.

`testCameraWorldRectConvention` locks this in, including the degenerate-zoom guard.

---

## 7. Multi-Platform CI Build & Package Verification `[VERIFIED]`

Verified on GitHub Actions workflow **`Build & Verify OptiGD`**, run **`38060134568`**, branch
`arena/0d1622f9-optigd`, commit `03ec664` (PR #6). **All 6 jobs passed.**

| CI Job | Job ID | Runner / Toolchain | Target | Verification Checks | Status |
|---|---|---|---|---|---|
| `Core Engine Tests & Benchmarks (ubuntu-latest)` | `114236537673` | Ubuntu Linux x86_64 (GCC) | Standalone C++20 | `ctest` (4/4 suites incl. 20/20 tests), `optigd_benchmark`, `jq -e '.optigd_version == "0.1.0-beta.2"'`, `jq -e '.evidence_classification == "[SIMULATED]"'`, `tools/generate_report.py` | **PASSED (`[VERIFIED]`)** |
| `Core Engine Tests & Benchmarks (windows-latest)` | `114236537804` | Windows Server x64 (MSVC) | Standalone C++20 | as above | **PASSED (`[VERIFIED]`)** |
| `Geode Mod (Windows x64)` | `114236820940` | Windows Server x64 (`clang++` MSVC ABI + Geode SDK `5.10.1`) | `Win64` | `coderunknow.optigd.dll` present in archive; `jq -e '.version == "v0.1.0-beta.2"'`; `jq -e '.id == "coderunknow.optigd"'` | **PASSED (`[VERIFIED]`)** |
| `Geode Mod (Android64 (arm64-v8a))` | `114236820916` | Ubuntu + Android NDK (`aarch64-none-linux-android23`) | `Android64` | `coderunknow.optigd.android64.so` present; `jq -e '.version == "v0.1.0-beta.2"'`; `jq -e '.id == "coderunknow.optigd"'` | **PASSED (`[VERIFIED]`)** |
| `Geode Mod (Android32 (armeabi-v7a))` | `114236820910` | Ubuntu + Android NDK (`armv7a-linux-androideabi23`) | `Android32` | `coderunknow.optigd.android32.so` present; `jq -e '.version == "v0.1.0-beta.2"'`; `jq -e '.id == "coderunknow.optigd"'` | **PASSED (`[VERIFIED]`)** |
| `Package Combined .geode Artifact` | `114237310787` | Ubuntu + `geode-sdk/build-geode-mod/combine` | Multi-platform `.geode` | Combined `coderunknow.optigd.geode` (**470,639 bytes**) contains all three binaries and `mod.json` with `v0.1.0-beta.2` | **PASSED (`[VERIFIED]`)** |

These `jq -e` assertions are hard gates: a mismatch fails the job. So the shipped
archive is proven to declare `v0.1.0-beta.2`, which is the packaging-level half of
the loading-defect fix.

**Third-party review bots**: `CodeRabbit` passed. `Kilo Code Review` reports
`action_required` with the summary *"Review could not start because the account has
insufficient credits"* — an account/billing condition on an external service, not a
finding against this code. No changes were made in response to it.

**Not re-downloaded**: the combined artifact could not be fetched into this
environment (network egress is restricted to `github.com`, `codeload.github.com`,
`api.github.com`, `registry.npmjs.org`, `pypi.org`, `files.pythonhosted.org`; the
artifact lives on `*.blob.core.windows.net`). The verification above therefore
rests on the CI job's own in-archive assertions rather than an independent local
re-inspection of the downloaded zip. This is stated rather than papered over.

---

## 8. Honest Status Summary

| Claim | Status |
|---|---|
| `v0.1.0-beta1` failed to load because of the `mod.json` version string | **PASS** (`[VERIFIED]` statically + `[MEASURED]` dynamically) |
| `v0.1.0-beta.2` is accepted by Geode `5.10.1`'s `VersionInfo` grammar | **PASS** (`[MEASURED]` via extracted-grammar harness + ported implementation) |
| `mod.json` / `Types.hpp` / `CMakeLists.txt` / CI version strings are consistent | **PASS** (`[MEASURED]` via `tools/validate_geode_metadata.py`) |
| 20/20 unit & integration tests pass; clean under ASan/UBSan | **PASS** (`[MEASURED]` locally, and `[VERIFIED]` on both `ubuntu-latest` and `windows-latest` CI) |
| Both JSON artefacts are schema-valid | **PASS** (`[MEASURED]`) |
| `mod.json` in the shipped archive declares `v0.1.0-beta.2` | **PASS** (`[VERIFIED]` via CI `jq -e` gates) |
| Synthetic benchmarks show 4.19x–5.45x reduction with 0 per-frame heap allocations | **PASS** (`[SIMULATED]`) |
| Multi-platform `.geode` builds (Win64 / Android32 / Android64) | **PASS** (`[VERIFIED]`) — 6/6 CI jobs, run `38060134568`, all three binaries + `mod.json` `v0.1.0-beta.2` |
| The mod loads and runs inside a real Geometry Dash `2.2081` client | **BLOCKED** — no GD binary, GPU context, or Geode loader in this environment |
| Any improvement in live in-game frame rate | **NOT CLAIMED / UNVERIFIED** — no in-game capture exists; none is implied by the `[SIMULATED]` numbers |

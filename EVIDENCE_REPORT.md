# OptiGD `v0.1.0-beta.3` Engineering Verification & Evidence Report

**Target Release**: `v0.1.0-beta.3` (Release Candidate)
**Target Game / Loader**: Geometry Dash `2.2081` / Geode SDK `v5.10.1`
**Target Platforms**: Windows x64 (`Win64`), Android 64-bit (`Android64` / `arm64-v8a`), Android 32-bit (`Android32` / `armeabi-v7a`)
**Scope of this release**: in-game Benchmark Mode, telemetry-integrity audit and repair, and the bug fixes found while doing both.
**Supersedes**: the `v0.1.0-beta.2` report (kept in git history), which superseded the `v0.1.0-beta1` report. The `v0.1.0-beta1` root-cause analysis below is retained unchanged because it is the reason the version string is spelled `v0.1.0-beta.3`.

**Headline honesty statement**: this release makes it possible to *measure* OptiGD in-game. It does not contain a measurement. No live-game capture, no external CapFrameX capture and no live Windows or Android runtime test was performed for beta.3, because this environment has none of the required hardware or software. Every artifact in this repository produced by a standalone binary is stamped `[SIMULATED]`, and the code that stamps them cannot say otherwise (see §4.4).

---

## 1. Evidence Classification Taxonomy

To ensure complete transparency and zero fabricated claims, every subsystem and metric in OptiGD is classified using four strict tags:

- **`[VERIFIED]`**: Verified by automated unit/integration tests (`ctest`) and/or multi-platform Geode SDK compilation (`Win64`, `Android32`, `Android64`).
- **`[MEASURED]`**: A number produced by an instrumented run in this environment (test suite, benchmark binary, or validator), reproducible with the commands given.
- **`[PARTIAL]`**: Core algorithm and unit tests are verified and hooks are wired safely, while live in-game Geometry Dash runtime validation remains pending.
- **`[SIMULATED]`**: Measured in the deterministic standalone C++20 stress benchmark suite (`optigd_benchmark`), not inside a live Geometry Dash `2.2081` process.
- **`[UNVERIFIED / BLOCKED]`**: Requires a runnable Geometry Dash `2.2081` + Geode `v5.10.1` graphical runtime environment, which is unavailable in headless CI/sandbox containers.
- **`[NOT RUN]`**: The check exists but did not execute in this environment. Stated explicitly rather than reported as a pass.
- **`[MEASURED-IN-GAME]`** (new in beta.3): a record produced by OptiGD's own instrumentation inside a live Geometry Dash process. Still an *internal* measurement: it cannot see GPU work, swap/present timing or driver-level pacing.
- **`[EXTERNAL]`** (new in beta.3): presented-frame data from an outside tool (CapFrameX on Windows x64). The only kind of number that can support a claim about how the game runs on a machine.

The three performance tags are kept apart by construction. `benchmark::evidence` holds the fixed
vocabulary, a session record carries its own tag, and the tag is stamped by the runtime that produced
the record — never inferred from the data, and only ever downgraded when a file is read.

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

`mod.json` now declares `"version": "v0.1.0-beta.3"` (beta.2 declared `v0.1.0-beta.2`; the grammar
analysis is unchanged and `tools/validate_geode_metadata.py` + `testGeodeVersionMetadataGrammar`
assert the current string, including that `v0.1.0-beta3` — no dot — is rejected).

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
| **`OptiGDManager`** (`src/core/OptiGDManager.*`) | `VERIFIED` | Initialized in `$on_mod(Loaded)` with live `Loader::get()->getVersion().toVString()`; frame lifecycle driven by `GJBaseGameLayer::update(dt)`. | Separates `totalFrameTimeUs` from the **wall** `updateCpuTimeUs`, the **thread** `updateThreadCpuUs` and `optigdOverheadUs`; propagates fallback/tier changes to `ConfigManager` only when the tier actually changed (so a benchmark override owns the active config); startup self-check on the version tag. **beta.3 fixes**: the fabricated `culledParticles = emitters * 32` estimate is gone; the `uint64 → uint32` matrix-cache delta truncation is gone; subsystem time recorded outside an open frame window is accounted separately (`getOutOfWindowSubsystemEvents()` / `…TimeUs()`) instead of being attributed to the next frame; `setFramePaused()` marks frames that are not active gameplay; stall classification uses the shared `core::kStallThresholdMs`. | `testManagerAndCompatibility` + `testNoFabricatedParticleEstimate` + `testMatrixCacheDeltaIsNotTruncated` + `testSubsystemTimeOutsideFrameWindowIsNotMisattributed` + `testStallClassificationUsesTheSharedThreshold` + `testThreadCpuTimeIsReportedSeparatelyFromWallTime`: full lifecycle, version check (`0.1.0-beta.3`, `v5.10.1`), arena & avoided-heap accounting, per-subsystem timing accessors, and each of the fixes above. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + cross-platform Geode hook compilation. |
| **`VisibilityCuller`** (`src/optimization/VisibilityCuller.*`) | `PARTIAL` | Hooked in `PlayLayer::addObject`, `PlayLayer::updateVisibility(dt)`, `PlayLayer::resetLevel()`, and `GJBaseGameLayer::updateObjectSection(GameObject*)`. | Runs *after* native `PlayLayer::updateVisibility(dt)`; only toggles visibility on decorative, non-trigger objects (`m_isDecoration \|\| m_isDecoration2`); respects `m_isHide`, `m_isInvisible`, `m_isDisabled`. Disabling culling mid-level now calls `restoreAllVisibility()` so hidden decorations are restored. | `testVisibilityCuller` + `testVisibilityCullerCellSizeReindex`: frustum culling, pointer-based bounds update, differential `evaluateVisibilityDelta`, `setCellSize()` mid-level re-index stability, `restoreAllVisibility()` / `hasCulledObjects()` round-trip. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + Geode `Win64`/`Android32`/`Android64` build; live GD visual check `[UNVERIFIED]`. |
| **`ParticleGovernor`** (`src/optimization/ParticleGovernor.*`) | `PARTIAL` | Hooked in `cocos2d::CCParticleSystem::update(float dt)` and `PlayLayer::addObject`. | Never sleeps `Critical` emitters (player trails) or finite-duration / `autoRemoveOnFinish` emitters; stores `baseEmissionRate` to prevent compounding decay; clamps `dt` and resets `m_fEmitCounter` on wake. | `testParticleGovernor` + `testParticleGovernorIncrementalTotals`: critical/finite-duration preservation, 50-frame non-compounding rate check, wake `dt` clamp, O(1) incremental totals across register/shrink/unregister. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + Geode build; live GD visual check `[UNVERIFIED]`. |
| **`TriggerOptimizer`** (`src/optimization/TriggerOptimizer.*`) | `PARTIAL` | Hooked in `PlayLayer::addObject`, `PlayLayer::setupHasCompleted`, `PlayLayer::postUpdate`, `PlayLayer::resetLevel`, `GJBaseGameLayer::spawnGroup`, `GJBaseGameLayer::toggleGroup`. | **Disabled by default** (`enable-trigger-indexing = false`). When enabled it only inspects: `inspectLookaheadWindow()` no longer mutates `activated`/`isCompleted`, so telemetry cannot change what native GD executes. Counters report `0` when indexing is off. | `testTriggerOptimizer`: `TouchOrPosition`, `TouchOnly` hitbox intersection, `isMultiTriggered`, deterministic `(x, spawnOrder, id)` sort, group toggle, backward cursor rewind, spawn-group dispatch. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + Geode build. Trigger counters are *not* an execution claim. |
| **`MatrixCache`** (`src/optimization/MatrixCache.*`) | `PARTIAL` | Used in `extractGameObjectBounds` (`PlayLayer::addObject`, `GJBaseGameLayer::updateObjectSection`) and evicted in `GJBaseGameLayer::removeObjectFromSection`. | `markDirty()` invalidates node and recursively invalidates children; `getCached()` rejects dirty **and never-computed** entries; `evict()` detaches parents/children; `pruneStaleEntries()` drops phantom nodes; `store()` is anchor-aware. | `testMatrixCache` + `testMatrixCacheStaleEntryPruning`: hit/miss, dirty invalidation, parent-child propagation, eviction, `AffineMatrix` equivalence, phantom-entry rejection, anchor keying, pruning. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + Geode build. |
| **`AdaptiveGovernor`** (`src/adaptive/AdaptiveGovernor.*`) | `VERIFIED` | Executed at end of each frame in `OptiGDManager::endFrame()`. | Rolling 120-frame mean, median (P50), P95, P99, std-dev using `MemoryArena` scratch; 120-frame hysteresis cooldown; locked when tier is `Custom`. Tier transitions apply `core::kTierDefaults` — the same table `ConfigManager` presets use. **Fixed in beta.3**: percentiles now come from `SampleWindow::medianFromSorted` / `percentileFromSorted` instead of `floor(n * p)` indexing. | `testAdaptiveGovernorAndFallback` + `testTierDefaultsSingleSource` + `testGovernorPercentilesUseTheSharedDefinition`: rolling window percentiles, automatic tier downgrade under budget pressure, arena usage, agreement between automatic transitions and hand-picked presets, and equality with the shared percentile definition at `n = 20` and `n = 120`. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass. |
| **`FallbackManager`** (`src/adaptive/FallbackManager.*`) | `VERIFIED` | Executed in `OptiGDManager::endFrame()` via `checkAndApplyFeatureFallbacks()`. | Detects frame-time jitter (`stdDevMs > threshold`) or per-feature CPU overruns, snapshots pre-fallback config, downgrades safely, restores after stable recovery frames. **Recovery is now evaluated every frame; the cooldown gates only new fallbacks.** | `testAdaptiveGovernorAndFallback` + `testFallbackRecoveryDuringCooldown`: trigger, downgrade, and recovery — including recovery while a cooldown is pending. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass. |
| **`Profiler`** (`src/profiling/Profiler.*`) | `VERIFIED` | Records per-subsystem durations in `OptiGDManager::endFrame()` and scoped timing via `OPTIGD_PROFILE_SCOPE`. | Hot path uses `recordById(MetricId, …)` — a fixed `std::array` slot, no mutex, no allocation. Ad-hoc names fall back to a mutex-guarded overflow map. | `testManagerAndCompatibility` + `testProfilerFastPath`: id/name paths share accumulators, min/max/mean correctness, `reset()` clears both. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + clean build. |
| **`DiagnosticsEngine`** (`src/diagnostics/DiagnosticsEngine.*`) | `VERIFIED` | Records per-frame stats in `OptiGDManager::endFrame()`; exports `optigd_diagnostics.json` on `PlayLayer::onQuit()`. | 100% local JSON telemetry (zero network). All interpolated strings are escaped. Emits a `notes` block declaring `native_trigger_execution_modified: false` and defining `total_triggers_skipped`. **beta.3**: backed by `SampleWindow` (capacity 60,000), level-scoped `reset()`, paused frames excluded and counted (`paused_frames`), `report_schema_version: 2` with window-disclosure keys, non-finite values sanitized to `0` behind `notes.non_finite_values_sanitized`, atomic export that checks its stream state. Every beta.2 key retained. | `testDiagnosticsJsonEscaping` + `testDiagnosticsWindowDisclosureIsConsistent` + `testDiagnosticsResetIsLevelScoped` + `testDiagnosticsExportIsAtomicAndChecked` + `testDiagnosticsSanitizesNonFiniteValues`, and ctest `OptiGDDiagnosticsJson` (real artefact, schema-validated out of process). | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + `tools/validate_diagnostics_json.py`. |
| **`DiagnosticsOverlay`** (`src/ui/DiagnosticsOverlay.*`) | `PARTIAL` | Rendered via `CCLabelBMFont` (`chatFont.fnt`) in `PlayLayer::init` and updated every 10 frames in `PlayLayer::postUpdate`. | Disabled by default (`enable-diagnostics-overlay = false`); displays `[OptiGD v0.1.0-beta.3 Diagnostics]`, FPS, Mean/P50/P95/P99, update time as **`X ms wall / Y ms thread`**, OptiGD Overhead, Culling delta, Particles, Triggers, Arena stats, and a footer stating that the numbers are internal telemetry from the 120-frame governor window — **not** presented-frame or GPU timing. | `testManagerAndCompatibility`: formatted HUD output, version header, the internal-telemetry disclosure, and the wall/thread split. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + Geode build. |
| **`CompatibilityLayer`** (`src/compatibility/CompatibilityLayer.*`) | `VERIFIED` | Initialized in `OptiGDManager::initialize(gdVer, geodeVer)` and checked before executing hooks. | GD gate is now an exact numeric comparison against `kTargetGDVersion` (`2.2081`), replacing a `starts_with("2.2")` prefix test that would have accepted GD `2.21x`. Per-feature conflict isolation without crashing. | `testManagerAndCompatibility` + `testGdVersionCompatibilityGate`: `2.2081`/`v2.2081`/`*` accepted; `2.21`, `2.2074`, `3.0`, `""`, `banana` rejected. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass. |
| **`ConfigManager`** (`src/config/ConfigManager.*`) | `VERIFIED` | Synchronized in `Hooks.cpp` (`syncSettingsWithConfig()`) via `ConfigManager::syncFromSettings()`; Benchmark Mode installs its transient override through `setRuntimeOverride`. | **beta.3**: two layers. `m_config` is the base (the only layer written by settings sync, deserialization, presets and `setTargetFrameRate`, and the only layer serialized); `m_override` is transient. `getConfig()` returns the override while armed. `setRuntimeOverride()` refuses without touching state when one is already armed; `updateRuntimeOverride()` re-asserts an override the caller owns; `clearRuntimeOverride()` refuses when nothing is armed. A capture therefore cannot corrupt saved settings. | `testConfigAndPresetSync` + `testConfigDeserializeRobustness` + `testConfigManagerTransientOverrideIsolation`: all 4 presets, file/string round-trip, malformed-line skipping, and override/base isolation including a refused re-arm leaving the first override intact. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass. |
| **`GeodeVersion`** (`src/core/GeodeVersion.*`) | `VERIFIED` | New in beta.2. Used by the startup self-check and the metadata validator. | Pure function, no allocation of state, no dependencies. | `testGeodeVersionMetadataGrammar`. | Standalone (`Linux`, `Win64`) | `ctest` pass + cross-checked against the extracted SDK harness. |
| **`JsonValue`** (`src/core/JsonValue.*`) | `VERIFIED` | New in beta.3. Serialization of benchmark session records and the store index. | Recursive-descent parser with an error offset; serializer escapes every string; non-finite doubles become `null` and set `sawNonFinite`. **Fixed**: `asDouble(fallback)` returned the fallback for a non-finite stored double, which turned "not a number" into a plausible `0`. | `testJsonValueRoundTripAndEscaping`: round-trip, escaping, depth/type errors, and the non-finite path. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass. |
| **`SampleWindow`** (`src/diagnostics/SampleWindow.*`) | `VERIFIED` | New in beta.3. Backing window for `DiagnosticsEngine` and for benchmark sessions; owns the percentile definition used by `AdaptiveGovernor` too. | Fixed-capacity ring + fixed-bin histogram + exact accumulators, O(1) push. Reports `percentile_method` (`exact` / `histogram_approximate`), `ringOverflow`, `histogramSaturated`. Counted / excluded / rejected outcomes are separate. | `testSampleWindowPercentileDefinitions` + `testSampleWindowRingOverflowDisclosesApproximation` + `testSampleWindowHitchStallAndRejectionAccounting`. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass. |
| **`BenchmarkTypes`** (`src/benchmark/BenchmarkTypes.*`) | `VERIFIED` | New in beta.3. Modes, states, statuses, warning codes, level identity, environment, evidence tags. | Every identity field carries an explicit "known" flag; an unknown identity never counts as a match. Warning codes are a fixed vocabulary shared with the Python validator. | `testBenchmarkSessionRejectsForeignAndNewerSchemas` and the compare/controller tests exercise the vocabulary. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + `tools/validate_benchmark_session_json.py`. |
| **`BenchmarkSession`** (`src/benchmark/BenchmarkSession.*`) | `VERIFIED` | New in beta.3. The persisted evidence record. | `fromJson` gates on `report_kind` and `schema_version`, refuses a newer schema, re-derives counters and warnings so a hand-edited file cannot claim more than its data supports, and can only **downgrade** the evidence classification. | `testBenchmarkSessionJsonRoundTrip` + `testBenchmarkSessionRejectsForeignAndNewerSchemas`. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass + emitted artifact validated out of process. |
| **`BenchmarkStore`** (`src/benchmark/BenchmarkStore.*`) | `VERIFIED` | New in beta.3. Persistence under `<GD save dir>/geode/mods/coderunknow.optigd/benchmarks/`. | Atomic write (temp + rename) so a crash cannot leave a half-written record; refuses to overwrite unless the bytes are identical (`errorCode == "already_exists"`); id allow-list so an id cannot escape the directory; tolerant listing; rebuildable `index.json`; no pruning. | `testBenchmarkStoreNeverOverwritesPriorEvidence` + `testBenchmarkStoreAtomicWriteLeavesNoPartialFile`. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass. |
| **`BenchmarkController`** (`src/benchmark/BenchmarkController.*`) | `VERIFIED` (mechanism) / `PARTIAL` (in-game) | New in beta.3. Driven from `Hooks.cpp` (`GJBaseGameLayer::update`, `PlayLayer::resetLevel` / `onQuit`, pause-edge detection) and from `BenchmarkUI`. | Verified transient override for both modes, re-checked every 16th frame; stalls → `gap_events`, paused frames → `paused_ms`, both excluded from the window; idempotent finalize that always clears the override **before** persisting; persist failure reported honestly with one retry under a fresh id; terminal state derived from the data. | `testBenchmarkControllerCompletesAndRestoresConfiguration` + `…ExcludesPausesAndStalls` + `…LevelResetHonoursTheSetting` + `…VerifiesItsOwnOverride` + `…ReportsPersistFailureHonestly`. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass. **Live in-game run `[NOT RUN]`.** |
| **`BenchmarkCompare`** (`src/benchmark/BenchmarkCompare.*`) | `VERIFIED` | New in beta.3. A/B comparison in the popup and in exported text. | Blocks instead of concluding on: no samples, non-comparable state, same mode twice, platform/architecture mismatch, level mismatch, duration Δ > 25 %, overlapping capture windows. Warns (non-blocking) on duration Δ > 10 % and on every integrity warning. Relative change from a zero baseline is reported as undefined with a note. `significanceClaimed` is `false` by construction. | `testBenchmarkCompareDirectionAndNoiseThreshold` + `…RefusesIncompatibleSessions` + `…NeverDividesByZero`. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass. |
| **`BenchmarkPanel`** (`src/ui/BenchmarkPanel.*`) | `VERIFIED` | New in beta.3. All user-facing text, including the HUD label and the CapFrameX procedure. | Platform-agnostic; approximated percentiles are rendered with a `~`; the baseline view states that it is an internal control, not vanilla GD. | `testBenchmarkPanelTextAndPagination`. | Standalone (`Linux`, `Win64`), Geode (`Win64`, `Android32`, `Android64`) | `ctest` pass. |
| **`BenchmarkUI`** (`src/geode/BenchmarkUI.*`) | `PARTIAL` | New in beta.3. `geode::Popup` (460×296, six views), the `PauseLayer::customSetup()` button, the runtime bridge, notifications. | The only place allowed to call `setLiveGameRuntime(true)`, i.e. the only code that can make a record claim `[MEASURED-IN-GAME]`. | Not unit-testable: needs the Geode loader and a game process. API usage checked by hand against the pinned GD `2.2081` bindings and the Geode `v5.10.1` SDK source. | Geode (`Win64`, `Android32`, `Android64`) — compiled by CI only | **Compilation `[VERIFIED]` on all three targets (CI run `38108365707`, commit `edc0048`) after defect V — the `cocos2d::CCMenuItemSpriteExtra` qualification — was fixed; the first run failed all three (§9.1). In-game behaviour `[NOT RUN]`.** |
| **`Hooks.cpp` benchmark integration** (`src/geode/Hooks.cpp`) | `PARTIAL` | `GJBaseGameLayer::update` drives the session; `PlayLayer` gate, pause-edge detection, HUD label, terminal-state notification; `PauseLayer::customSetup()` button; two setting listeners. | Telemetry and benchmark frames are gated on an active `PlayLayer`, so `LevelEditorLayer` frames are not gameplay. The frame delta is the raw monotonic interval, never GD's clamped `dt`. | Not unit-testable locally (Geode-only translation unit). | Geode (`Win64`, `Android32`, `Android64`) — compiled by CI only | **Compilation `[VERIFIED]` on all three targets (CI run `38108365707`, commit `edc0048`); in-game behaviour `[NOT RUN]`.** |

---

## 4. Benchmark Mode Evidence (new in beta.3)

### 4.1 What `[VERIFIED]` means here, precisely

The **mechanism** is verified: the state machine, the transient configuration
layer, the window accounting, the record format, the store's refusal to destroy
prior evidence, and the comparator's refusal to conclude. All of it is exercised by
tests that run the real classes, not mocks of them.

The **in-game behaviour** is not: no Geometry Dash process, no Geode loader, no GPU
and no display exist in this environment, so nothing was ever seen on screen. The
pause-menu button placement, the popup layout and the HUD label are derived from the
pinned GD `2.2081` bindings and the Geode `v5.10.1` SDK source and are
**`[NOT RUN]`** — they have not been visually verified.

| Requirement | Status | Evidence |
|---|---|---|
| Reachable in-game entry point with start / timeout / cancel / results | `VERIFIED` (code + test) / `NOT RUN` (in game) | `PauseLayer::customSetup()` button, `BenchmarkPopup` six views, `BenchmarkController` start/auto-finalize/cancel; `testBenchmarkControllerCompletesAndRestoresConfiguration`, `…LevelResetHonoursTheSetting`. Live click path unobserved. |
| Baseline vs optimized via a **transient** config that never corrupts saved settings | `VERIFIED` | Two-layer `ConfigManager`; base layer is the only one serialized; `testConfigManagerTransientOverrideIsolation`, `testBenchmarkControllerCompletesAndRestoresConfiguration` (asserts the user config is byte-identical after a capture). |
| Baseline disclosed as an internal control, not vanilla GD | `VERIFIED` | `mode_display`, `measurement_definitions.baseline_mode`, `features_disabled_for_baseline` read back from the applied config; `testMakeBaselineConfigIsInternalControlNotVanilla`; the Python validator fails a baseline record that omits the disclosure or that turns the diagnostics overlay off. |
| Internally consistent boundaries / samples / counters / percentiles | `VERIFIED` | `frames_observed == frames_counted + frames_excluded + frames_rejected`; percentile ordering; `captured ≤ wall`; state derived from data. Asserted in C++ **and** out of process on the emitted artifact. |
| Save / reopen / compare / export without overwriting prior results | `VERIFIED` | `testBenchmarkStoreNeverOverwritesPriorEvidence`, `testBenchmarkStoreAtomicWriteLeavesNoPartialFile`, `testBenchmarkSessionJsonRoundTrip`. |
| Comparisons flag incompatible sessions instead of manufacturing conclusions | `VERIFIED` | `testBenchmarkCompareRefusesIncompatibleSessions`, `testBenchmarkCompareNeverDividesByZero`, `testBenchmarkCompareDirectionAndNoiseThreshold`. |
| Internal instrumentation distinguished from external presented-frame measurement | `VERIFIED` (labels, tags, definitions) / `NOT RUN` (external capture) | `evidence_classification` on every record; overlay footer; `measurement_definitions`; `limitations`; `docs/BENCHMARKS.md` §1. No external capture exists (§5). |
| No fabricated gameplay / forced completion / automated input / physics change | `VERIFIED by construction` | Benchmark Mode only observes `GJBaseGameLayer::update` frames and installs a configuration override. It calls no input API, does not touch `m_hasCompletedLevel`, and does not modify trigger execution (`notes.native_trigger_execution_modified == false`, asserted by `tools/validate_diagnostics_json.py`). |
| No telemetry upload / network reporting / analytics | `VERIFIED` | No socket, HTTP or analytics symbol anywhere in `src/`; every artifact is a local file under the mod's save directory. |

### 4.2 The artifact that does exist `[SIMULATED]`

`optigd_tests --emit-sessions <dir>` runs the real `BenchmarkController`,
`BenchmarkSession`, `BenchmarkStore` and `BenchmarkCompare` against a synthetic
clock and a temporary directory, then writes two session records, an index and a
comparison. Validated by ctest `OptiGDBenchmarkSessionJson`.

What the emitted baseline/optimized pair contains (fixture values, synthetic clock):

| | baseline | optimized |
|---|---|---|
| Session id | `20251009T085319Z-baseline-0001` | `20251009T085422Z-optimized-0002` |
| State / comparable | `completed` / true | `completed` / true |
| Frames (observed = counted + excluded + rejected) | 150 = 150 + 0 + 0 | 188 = 188 + 0 + 0 |
| Captured / wall | 3000 ms / 3000 ms | 3008 ms / 3008 ms |
| Mean frame time | 20.000 ms | 16.000 ms |
| Avg FPS | 50.000 | 62.500 |
| Evidence classification | `[SIMULATED]` | `[SIMULATED]` |

**These numbers are fixtures.** The synthetic clock was fed 20 ms and 16 ms frame
intervals; the "+25.00 %" in `comparison.txt` is arithmetic on those two fixtures.
It is not a measurement of Geometry Dash, it is not a measurement of OptiGD's effect
on anything, and it must not be quoted as one. The record says so in
`evidence_classification`, in `evidence_classification_note`, and in the provenance
footer of `comparison.txt`; the validator fails the build if any of the three is
missing.

### 4.3 The validator is not a no-op `[MEASURED]`

`tools/validate_benchmark_session_json.py` was checked by tampering with 13 copies
of a valid artifact set. Every tampered copy was rejected with exit 1 and a specific
message:

| Tampering | Detected |
|---|---|
| `frames_observed` inflated by 7 | yes — frame budget identity |
| `evidence_classification` changed to `[MEASURED-IN-GAME]` | yes — standalone artifacts must be `[SIMULATED]` |
| `completed` with zero samples | yes — 3 errors (contradiction, `comparable`, ring used) |
| P99 below P95 | yes — percentile ordering |
| `integrity.paused_ms` removed | yes — missing key |
| `captured_duration_ms` > `wall_duration_ms` | yes — window consistency |
| `override_applied` false | yes — a session without a verified override is not evidence |
| Baseline passthrough disclosure emptied | yes — 2 errors |
| `avg_fps` set to NaN | yes — non-finite must be `null`, not a number |
| Unknown warning code injected | yes — fixed vocabulary |
| `index.session_count` wrong | yes — index/session agreement |
| Two files claiming one session id | yes — duplicate id |
| `comparison.txt` provenance footer removed | yes — 2 errors |

### 4.4 Why the classification cannot be forged

`BenchmarkSession::toJson()` used to hardcode `[MEASURED-IN-GAME]`, which meant the
artifacts above asserted they had been captured inside a live game. The tag is now a
field on the record, stamped by `BenchmarkController` from the runtime it is
attached to: `setLiveGameRuntime(true)` is called only from
`installBenchmarkRuntimeBridge()` in `src/geode/BenchmarkUI.cpp`, which runs inside
the Geode loader. The default is `[SIMULATED]`, and `fromJson` may only **downgrade**
an unrecognized tag. `testBenchmarkSessionJsonRoundTrip` asserts all three
directions, including that a tampered `[PROBABLY-REAL]` tag loads as `[SIMULATED]`.

---

## 5. External CapFrameX Verification (Windows x64) `[NOT RUN]`

### 5.1 What was delivered

| Item | Status |
|---|---|
| Documented procedure | `VERIFIED` — in-game (**Benchmark → External**, `BenchmarkPanel::formatExternalProcedure()`) and in `docs/BENCHMARKS.md` §3. Same text, one source. |
| Comparison tool | `VERIFIED` — `tools/compare_capframex.py`, ctest `OptiGDExternalComparisonTool` (`--self-test`). Reads comma/semicolon/tab exports, an FPS-only export, rejects blank/non-numeric/non-positive rows, refuses degenerate and unrecognized files, uses the same nearest-rank percentile definition as the internal statistics, reports the 1 % low FPS and the run-to-run spread, and states that no significance is claimed. |
| An actual external capture | **`NOT RUN` / `BLOCKED`** — requires a Windows x64 host with a GPU, Geometry Dash `2.2081`, Geode `v5.10.1` and CapFrameX. None is available here. |
| External numbers for OptiGD | **none exist.** No external FPS, frametime or 1 % low figure appears anywhere in this repository, and none is implied by the tool working. |

### 5.2 Limits of the tool's own verification

The `--self-test` fixtures are **synthetic CSVs written by the test itself**. No real
CapFrameX export could be obtained in this environment (network egress is restricted
to `github.com`, `codeload.github.com`, `api.github.com`, `registry.npmjs.org`,
`pypi.org`, `files.pythonhosted.org`), so the column-header matching is heuristic
rather than pinned to a verified sample, and CapFrameX version differences in export
layout are handled by detection plus `--frametime-column` / `--fps-column`
overrides. A file the reader cannot interpret fails with the columns it found; it
never guesses numbers into existence. This limit is stated in the script's docstring,
in `docs/BENCHMARKS.md` §3.3, and here.

### 5.3 Android

No practical equivalent of CapFrameX exists for Android 32/64, so Android results
are internal telemetry only. That is stated in the in-game External view and in
`docs/BENCHMARKS.md`, rather than leaving a reader to assume the Windows procedure
transfers.

---

## 6. Synthetic Stress Benchmark Evidence `[SIMULATED]`

Captured by `./build/optigd_benchmark` in this environment (Linux x86_64, GCC 12.2.0, `-O3`, fixed
deterministic seed `42`, **median of 5 runs**) **for `v0.1.0-beta.2`**. They were **not re-run for
beta.3**: beta.3 changed telemetry accounting, configuration layering and the percentile definition,
not the culling / particle / matrix hot paths these scenarios exercise. They are reproduced here
unchanged and labelled with the release they came from, rather than being silently re-dated.

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

## 7. Real-Game Geometry Dash Runtime Benchmarks `[UNVERIFIED / BLOCKED]`

### Status: `BLOCKED` by Headless Environment

- **Why Blocked**: Executing real-game benchmarks requires a licensed Geometry Dash `2.2081` binary, an active OpenGL/GLES GPU context, and the Geode `v5.10.1` runtime loader on Windows x64 or Android. None of these exist inside the headless Linux build/CI container. There is also no Windows host (so no CapFrameX) and no Android device or emulator.
- **No substitute is claimed.** Every `[SIMULATED]` number in this report is a standalone harness result; none of it is a proxy for in-game frame time.
- **The Geode-only translation units (`src/geode/Hooks.cpp`, `src/geode/BenchmarkUI.cpp`) cannot be compiled here either**: the Geode CLI, bindgen and target binaries are distributed as GitHub release assets, and release-asset downloads are blocked in this sandbox. They are compiled by CI on Win64 / Android64 / Android32. Every OptiGD API they call was checked by hand against the headers, and every GD/Geode API against the pinned `2.2081` bindings and the Geode `v5.10.1` SDK source, but that is a static review, not a compile.

### Built-In Harness for Hardware Testers

**Primary path — Benchmark Mode (new in beta.3):**

1. Install `coderunknow.optigd.geode` into Geometry Dash `2.2081` with Geode `v5.10.1`.
2. Enter the level, press **Esc**, click **OptiGD Benchmark**.
3. Click **Baseline 60s** and play normally for 60 s of active gameplay. The session
   finalizes itself; the configuration is restored before anything is written.
4. Restart the attempt, click **Optimized 60s**, play the same segment.
5. Open **Compare**, then **Export**. Files land in
   `<GD save dir>/geode/mods/coderunknow.optigd/benchmarks/` (+ `exports/`).
6. Repeat for at least 3–5 pairs, alternating the order, and compare the **spread**
   across pairs (see **Protocol** in the popup).

Records are stamped `[MEASURED-IN-GAME]` only when produced this way. Validate them
off-machine with:

```bash
python3 tools/validate_benchmark_session_json.py <dir-of-session-json> --allow-in-game
```

**Secondary path — external presented frames (Windows x64 only):** the CapFrameX
procedure in §5 and `docs/BENCHMARKS.md` §3. This is the only path that can support
a claim about how the game runs on a machine.

**Legacy path — per-level diagnostics report:** disable the four optimization
settings by hand, play, exit, copy `optigd_diagnostics.json`; re-enable them, play
the same segment, copy again; then

```bash
python3 tools/generate_report.py --compare baseline_level.json optigd_level.json
```

which outputs a Markdown table of Average/Min/Max FPS, Mean/Median(P50)/P95/P99 frame
times, Mean Update CPU time, Mean OptiGD Overhead, Culled Objects, Avoided Particles
and Avoided Heap Allocations. It is a descriptive diff of two internal reports: it
does not test significance and cannot tell whether the two runs were comparable.
Prefer Benchmark Mode, which records the configuration that was actually in force and
refuses incomparable pairs.

---

## 8. Automated Test & Validation Evidence `[MEASURED]`

All executed in this environment on the standalone engine build (Linux x86_64, GCC 12.2.0, `-O3`).

| Check | Command | Result |
|---|---|---|
| Unit & integration suite | `ctest -R OptiGDUnitTests` (`./build/optigd_tests`) | **PASS** — 51/51 test functions, exit 0 |
| Diagnostics artefact schema | `ctest -R OptiGDDiagnosticsJson` | **PASS** — real report emitted, 0 schema errors |
| `mod.json` metadata | `ctest -R OptiGDGeodeMetadataValidation` | **PASS** — 0 errors |
| Benchmark **session** artefacts | `ctest -R OptiGDBenchmarkSessionJson` | **PASS** — 2 `[SIMULATED]` sessions + index + comparison emitted by the real controller, 0 schema errors |
| External comparison tool | `ctest -R OptiGDExternalComparisonTool` | **PASS** — `compare_capframex.py --self-test` on synthetic CSV fixtures |
| Setting-type audit vs. `mod.json` | `ctest -R OptiGDSettingTypeAudit` | **PASS** — 16 declared settings, 32 call sites in `src/`, 0 mismatches |
| Auditor mutation test | `ctest -R OptiGDSettingTypeAuditSelfTest` | **PASS** — 13/13 synthetic fixtures; additionally re-run against the real tree with defect W reintroduced: exit 1 naming `src/geode/BenchmarkUI.cpp:209`, exit 0 with the fix restored |
| Synthetic benchmark artefact schema | `ctest -R OptiGDBenchmarkJsonSchema` | **PASS** — 3 scenarios, `[SIMULATED]`, 0 heap allocs |
| Full suite | `ctest --test-dir build --output-on-failure` | **PASS** — 8/8 |
| Compiler warnings | `-Wall -Wextra -Wpedantic` | **Clean** — zero warnings across all four standalone targets |
| Address + UB sanitizers | `cmake -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all"`, then `ctest` + both binaries | **PASS** — 51/51 tests, 8/8 ctest, `optigd_benchmark` completed; no ASan/UBSan findings, no leaks |
| CI staging step, dry-run | the `Stage Package For The Publishing Job` `run:` script against a stand-in build output | **PASS** — exit 0, the SHA-256 of the original and of the staged copy printed and equal; an archive missing its declared binary exits 1 |
| CI publishing steps, dry-run + mutation test | all four `Prepare Named Package` `run:` scripts extracted from `build.yml` and executed against stand-in source artefacts, then 6 mutations of the Win64 one | **PASS** — all four exit 0 with the correct file names, `SHA256SUMS.txt` and `INSTALL.txt`; **6/6 mutations detected** (foreign `.so`, missing `.dll`, two `.geode` files, none, inner `mod.json` with another version, another id), each exit 1 with a specific message. A dry run of the *scripts*, not of the Geode build: no `.geode` can be produced here. |
| A superseded CI step, dry-run | the combined-package step as first written, in a directory holding only the archive — the condition the real combine job runs in | **FAIL by design** — exit 2 with `jq: error: Could not open file mod.json`. A real latent defect (that job has no checkout), but **not** the cause of the CI failures, which was the upstream combine action itself (§9.1, Runs 4–5). |
| Workflow structure | `yaml.safe_load` over `build.yml`, job graph and per-job step lists printed; the upstream `combine/action.yml` and `action.yml` read through `api.github.com` | **PASS** — parses; graph is `standalone → build-geode-mod → combine → publish-installable-packages → verify-published-artifacts`; the upstream action's `find … -name '*.geode'` glob and its `delete-artifacts` input are confirmed in its source rather than assumed |
| Artefact-verification job, dry-run + mutation test | the `Verify Every Published Artefact` `run:` script against synthetic artefact directories, then 10 mutations of them | **PASS** on the correct set (7 checks per platform artefact, 8 combined); **10/10 mutations detected**, each exit 1 with a specific message: altered bytes, missing `.dll`, foreign `.so`, wrong commit in the name, missing `INSTALL.txt`, altered inner `mod.json`, missing `SHA256SUMS.txt`, falsified `SHA256SUMS.txt`, nothing published, renamed package |
| PR-comment step, dry-run | the `Record Checksums On The Pull Request` `run:` script against a stub `gh` | **PASS** — exit 0, rendered markdown inspected (it first failed on a shell quoting bug, recorded in `DESIGN_LOG.md` §7.7) |
| Validator mutation check | 13 hand-tampered session artifacts (§4.3) | **13/13 detected**, each exit 1 with a specific message |
| Geode-only translation units | `src/geode/Hooks.cpp`, `src/geode/BenchmarkUI.cpp` | **NOT RUN locally** — no Geode SDK/CLI/bindgen reachable; compiled by CI (§9) |

Build configuration used for every local result above:
`cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPTIGD_STANDALONE_ONLY=ON`
(Debian 12 x86_64, g++ 12.2.0, cmake 4.4.4, ninja 1.13.2).

Test inventory (**51**). Pre-existing (9): `testMemoryArena`, `testSpatialHash`, `testVisibilityCuller`, `testParticleGovernor`, `testTriggerOptimizer`, `testMatrixCache`, `testAdaptiveGovernorAndFallback`, `testConfigAndPresetSync`, `testManagerAndCompatibility`. Added for beta.2 (11): `testGeodeVersionMetadataGrammar`, `testVisibilityCullerCellSizeReindex`, `testParticleGovernorIncrementalTotals`, `testDiagnosticsJsonEscaping`, `testConfigDeserializeRobustness`, `testCameraWorldRectConvention`, `testMatrixCacheStaleEntryPruning`, `testFallbackRecoveryDuringCooldown`, `testProfilerFastPath`, `testGdVersionCompatibilityGate`, `testTierDefaultsSingleSource`. Added for beta.3 (31): `testGovernorPercentilesUseTheSharedDefinition`, `testJsonValueRoundTripAndEscaping`, `testSampleWindowPercentileDefinitions`, `testSampleWindowRingOverflowDisclosesApproximation`, `testSampleWindowHitchStallAndRejectionAccounting`, `testConfigManagerTransientOverrideIsolation`, `testMakeBaselineConfigIsInternalControlNotVanilla`, `testBenchmarkSessionJsonRoundTrip`, `testBenchmarkSessionRejectsForeignAndNewerSchemas`, `testBenchmarkStoreNeverOverwritesPriorEvidence`, `testBenchmarkStoreAtomicWriteLeavesNoPartialFile`, `testBenchmarkControllerCompletesAndRestoresConfiguration`, `testBenchmarkControllerExcludesPausesAndStalls`, `testBenchmarkControllerLevelResetHonoursTheSetting`, `testBenchmarkControllerVerifiesItsOwnOverride`, `testBenchmarkControllerReportsPersistFailureHonestly`, `testBenchmarkCompareDirectionAndNoiseThreshold`, `testBenchmarkCompareRefusesIncompatibleSessions`, `testBenchmarkCompareNeverDividesByZero`, `testBenchmarkPanelTextAndPagination`, `testDiagnosticsWindowDisclosureIsConsistent`, `testDiagnosticsResetIsLevelScoped`, `testDiagnosticsExportIsAtomicAndChecked`, `testDiagnosticsSanitizesNonFiniteValues`, `testNoFabricatedParticleEstimate`, `testMatrixCacheDeltaIsNotTruncated`, `testSubsystemTimeOutsideFrameWindowIsNotMisattributed`, `testStallClassificationUsesTheSharedThreshold`, `testThreadCpuTimeIsReportedSeparatelyFromWallTime`, `testPlatformArchitectureIsReported`, `testBenchmarkWallClockCeilingIsAGuardNotABudget` (added in the real-game handoff pass, `DESIGN_LOG.md` §7.7).

### Camera convention note `[VERIFIED by binding inspection]`

`GJBaseGameLayer::m_gameState.m_cameraPosition` is the **bottom-left** of the visible area in object-layer units, not its centre; the view spans `[camX, camX + winSize.width / zoom]`. Evidence from the pinned GD `2.2081` bindings:

- `inline/PlayLayer.cpp screenFlipObject()` mirrors `x` about `camX + (winSize.width / zoom) / 2`.
- `inline/GJBaseGameLayer.cpp lightningFlash()` uses `m_cameraPosition.y + (winSize.height + 50) / scale` for the top edge.

`testCameraWorldRectConvention` locks this in, including the degenerate-zoom guard.

---

## 9. Multi-Platform CI Build & Package Verification

### 9.1 `v0.1.0-beta.3` — this release

**Status: `[VERIFIED]` on the second attempt.** beta.3 needed two CI runs and both are
recorded here, because the first one failed and the failure is part of this release's
history. `src/geode/*` cannot be compiled in the sandbox this release was built in (no
Android NDK, no clang 19, no C++23 toolchain, no Geometry Dash binary), so CI is the
only compile verification those two translation units have ever had.

#### Run 1 — commit `4c686ef`: **FAILED** (3 of 6 jobs)

Workflow `Build & Verify OptiGD`, runs `38107167242` (push event) and `38107240434`
(pull-request event); both produced identical results.

| CI Job | Job ID | Status |
|---|---|---|
| `Core Engine Tests & Benchmarks (ubuntu-latest)` | `114375009286` | PASSED |
| `Core Engine Tests & Benchmarks (windows-latest)` | `114375009257` | PASSED |
| `Geode Mod (Windows x64)` | `114375270323` | **FAILED** |
| `Geode Mod (Android64 (arm64-v8a))` | `114375270226` | **FAILED** |
| `Geode Mod (Android32 (armeabi-v7a))` | `114375270262` | **FAILED** |
| `Package Combined .geode Artifact` | `114375720037` | skipped (dependency failed) |

**Cause (defect V, introduced by beta.3 and found by CI).** `src/geode/BenchmarkUI.hpp`
declared the popup's button map as `std::map<Btn, cocos2d::CCMenuItemSpriteExtra*>`.
`CCMenuItemSpriteExtra` is a *Geometry Dash* class that the generated bindings declare in
the **global** namespace (`class CCMenuItemSpriteExtra : public
cocos2d::CCMenuItemSprite`), so the qualified name does not exist. The declaration failed
to parse inside the class body, which invalidated the whole `BenchmarkPopup` definition
and cascaded into 13 errors across the only two files the Geode target compiles and the
standalone jobs do not (`BenchmarkUI.cpp`: 10, `Hooks.cpp`: 3). Every core source —
including all nine new beta.3 translation units — compiled cleanly for Android64 under
clang/C++23/LTO before the build reached those two files.

**How the cause was established, since the logs were not readable.** The runner logs for
those jobs are not retrievable from this environment: `GET /actions/jobs/{id}/logs`
redirects to `productionresultssa8.blob.core.windows.net`, outside the network allowlist,
and the check-run annotations carry only `Process completed with exit code 1`. A
temporary workflow (`.github/workflows/geode-diagnose.yml`, commit `8a3927a`) therefore
replayed `geode-sdk/build-geode-mod@main`'s own configure and build commands — copied
from its `action.yml` at `a0f7676e`, same NDK toolchain file, same clang, same
`-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON` — captured stdout/stderr to a file, and posted
the filtered compiler output as a comment on PR #7, which *is* readable through the API.
That run reproduced the failure exactly and named the line. The temporary workflow was
deleted in the commit that records this section; it never gated anything. Full narrative
in `DESIGN_LOG.md` §6.12.

#### Run 2 — commit `edc0048` (the fix): **ALL 6 JOBS PASSED**

Workflow `Build & Verify OptiGD`, runs `38108365707` (push event) and `38108363696`
(pull-request event). Both are green; the push run is tabulated.

| CI Job | Job ID | Runner / Toolchain | Target | Verification Checks | Status |
|---|---|---|---|---|---|
| `Core Engine Tests & Benchmarks (ubuntu-latest)` | `114378570355` | Ubuntu Linux x86_64 (GCC, C++20) | Standalone engine | `ctest` 6/6 suites (50/50 unit tests), `optigd_benchmark`, `jq -e '.optigd_version == "0.1.0-beta.3"'`, `jq -e '.evidence_classification == "[SIMULATED]"'`, `tools/generate_report.py` | **PASSED (`[VERIFIED]`)** |
| `Core Engine Tests & Benchmarks (windows-latest)` | `114378570482` | Windows Server x64 (MSVC, C++20) | Standalone engine | as above | **PASSED (`[VERIFIED]`)** |
| `Geode Mod (Windows x64)` | `114378862760` | Windows Server x64, `clang++` MSVC ABI + Geode SDK `5.10.1`, C++23 | `Win64` | `coderunknow.optigd.dll` present in the archive; `jq -e '.version == "v0.1.0-beta.3"'`; `jq -e '.id == "coderunknow.optigd"'` | **PASSED (`[VERIFIED]`)** |
| `Geode Mod (Android64 (arm64-v8a))` | `114378862767` | Ubuntu + Android NDK `29.0.14206865` clang (`aarch64-none-linux-android23`), `-flto=thin` | `Android64` | `coderunknow.optigd.android64.so` present; same two `jq -e` gates | **PASSED (`[VERIFIED]`)** |
| `Geode Mod (Android32 (armeabi-v7a))` | `114378862722` | Ubuntu + Android NDK `29.0.14206865` clang (`armv7a-linux-androideabi23`) | `Android32` | `coderunknow.optigd.android32.so` present; same two `jq -e` gates | **PASSED (`[VERIFIED]`)** |
| `Package Combined .geode Artifact` | `114379175513` | Ubuntu + `geode-sdk/build-geode-mod/combine` | Multi-platform `.geode` | Combined `coderunknow.optigd.geode` (**1,037,855 bytes**, artifact `coderunknow.optigd-geode-package`) contains all three binaries and a `mod.json` whose `.version` is `v0.1.0-beta.3` | **PASSED (`[VERIFIED]`)** |

The `jq -e` assertions are hard gates, so the shipped archive is *proven* to declare
`v0.1.0-beta.3` — the version-grammar half of criterion 9 checked at packaging level,
independently of `tools/validate_geode_metadata.py`. No build target was removed or
disabled to obtain this green run: the only change between run 1 and run 2 is one type
qualification and one `override` keyword in `src/geode/BenchmarkUI.hpp`, and beta.3 still
adds `BenchmarkUI.cpp` to the Geode library plus nine new core translation units and two
new ctest checks relative to beta.2.

**What this does and does not prove.** These jobs compile, link and package. They never
execute the mod: no Geometry Dash 2.2081 binary, no Geode loader, no GPU and no Android
device exist in CI or in the build sandbox, so nothing here demonstrates that Benchmark
Mode opens, measures or saves anything in the real game. "All 6 CI jobs passed" means
*builds and packages on three targets*, and it is reported as exactly that everywhere in
this document.

#### Run 3 — commit `0327231`: **ALL 6 JOBS PASSED** (the last run before the handoff pass)

Runs `38108968555` (`pull_request`) and `38108967055` (`push`), both `success` on all six
jobs, twelve check-runs green. The published artefacts were the three that existed at the
time: `coderunknow.optigd-geode-package` (1,038,287 bytes, the combined archive) and the two
standalone JSON artefacts. `mergeable_state` was `unstable` solely because of the external
`Kilo Code Review` check (`action_required`, insufficient credits), which is not a job of
this repository's workflow.

#### Runs 4, 5 and 6 — commits `3587076`, `f3952b2`, `1775194` (the handoff pass): **all builds green; `combine` failed twice, then passed**

Runs `38207332745` / `38207336767` (commit `3587076`) and `38208852829` / `38208855985`
(commit `f3952b2`). In all four, both standalone jobs and **all three Geode target jobs
succeeded** — the first time the `src/geode/*` changes of the handoff pass were compiled
anywhere, on Windows x64, Android64 and Android32. Run `38207332745` published
`OptiGD-v0.1.0-beta.3-Win64-3587076` (364,321 bytes) and its two Android counterparts.

`Package Combined .geode Artifact` **failed on both commits**, and `Verify Published Artefacts
& Record Checksums` was skipped because it depends on it. Neither run published a combined
artefact. Recorded as failures, with their cause, not glossed over.

**The first diagnosis was wrong.** The combine job had gained a step reading the version with
`jq -r '.version' mod.json`, and that job never checks the repository out; reproduced locally
in a directory holding only the archive, the step exits `2` with `jq: error: Could not open
file mod.json: No such file or directory`. A real defect — but a latent one, because the job
died before reaching it. What settled it was
`GET /repos/{owner}/{repo}/actions/jobs/{id}` → `.steps[]`, which returns a conclusion per step
without needing log access: step 2, `Combine Platform Binaries` (the upstream composite action)
`failure`, steps 3–6 `skipped`. Annotations only said "Process completed with exit code 1".

**The actual cause.** `geode-sdk/build-geode-mod/combine@main` downloads *every* artefact in
the run (`actions/download-artifact@v8`, no pattern) and merges whatever
`find ./artifacts -name '*.geode' -type f | sort -u` returns, looking for each name inside the
per-platform directories `geode-build-win`, `geode-build-mac`, `geode-build-android32`,
`geode-build-android64`, `geode-build-ios`. The new
`OptiGD-<version>-<target>-<short-sha>` artefacts each carried a differently named `.geode` at
their own root, so for those names the action found no per-platform match, its lone-artefact
fallback also missed (the file sits in a subdirectory), and it invoked `geode package merge`
with **no input files**, which exits non-zero. Two observations corroborate it: the merge step
is where the job died, and the `geode-build-*` artefacts survived both runs — which they only
do when the action fails before its `delete-artifact` step.

**The fix.** The named packages moved out of the build jobs and out of the combine job into a
new `publish-installable-packages` job that runs *after* combine, so no artefact carrying a
renamed `.geode` exists while the upstream action is globbing the run; combine is now called
with its own documented `delete-artifacts: false` input so the per-platform archives are still
there to publish from. The build job is otherwise unchanged from the version that passed in Run
3, and the `jq`-from-disk step was deleted rather than patched, since the publishing job has a
checkout. No target, check or gate was removed or weakened to get past the failure.

The lesson that generalizes is in `DESIGN_LOG.md` §7.10: a dry run of one step cannot see what
another job's action does with the artefacts a run contains, so before a step publishes
anything, read what else in the run consumes artefacts by globbing.

**Run 6 — commit `1775194`, runs `38211200450` / `38211204184` — tested that fix and found the
next problem.** Both standalone jobs, all three Geode target jobs and **`Package Combined .geode
Artifact` passed**, the first time the combine job had passed since the handoff work began, and
the combined leg of the new publishing job uploaded `OptiGD-v0.1.0-beta.3-combined-1775194`
(1,042,702 bytes) after checking the archive's binaries and its inner `mod.json`. The three
platform legs failed at `Download Source Artefact`, with the annotation naming the cause:
`Unable to download artifact(s): Artifact not found for name: geode-build-win`. That commit
passed `delete-artifacts: false` to the combine action on the strength of its documented input;
the action gates the deletion on `if: ${{ inputs.delete-artifacts }}`, composite inputs are
strings, and the string `"false"` is truthy, so the artefacts were deleted regardless — the
run's artefact list confirms none remained. The fix is a staged copy: each build job uploads its
own archive a second time as `optigd-source-<target>` under a `.bin` file name that the upstream
`find … -name '*.geode'` glob does not match, and the publishing job renames it back after
re-verifying it. The inert input is no longer passed.

Also confirmed on these runs rather than assumed: `pull_request`-triggered runs build GitHub's
merge-preview commit, so their artefacts carry that commit's short SHA (`-49490f1` on run
`38207336767`), not the head's. The artefact matching the head commit exactly comes from the
`push` run.

#### The installable artefacts, and where their run is recorded

`publish-installable-packages` runs after `combine` with four matrix legs — Win64, Android64,
Android32 and combined. Each leg downloads the artefact that already holds that platform's
archive (the staged `optigd-source-Win64` / `-Android64` / `-Android32` copies, and
`coderunknow.optigd-geode-package` for the combined one), proves it is the platform it is about
to be named for
(exactly one `.geode`; that target's binaries present; every other target's binaries absent;
the `id` and `version` inside the archive's own `mod.json` matching the repository's), renames
it to `coderunknow.optigd-<version>-<target>.geode`, writes `SHA256SUMS.txt` and a
platform-specific `INSTALL.txt`, and uploads the three as
`OptiGD-<version>-<target>-<short-sha>` with `if-no-files-found: error`.

`verify-published-artifacts` then downloads those artefacts back out of Actions storage and
re-checks them as a tester would — artefact and package names carrying version, platform and
commit; `sha256sum -c` against the checksum file inside the artefact; the right binaries
present and no foreign ones; the archive's `mod.json`; `INSTALL.txt` — and gates on all of it.
Because job logs, artefact bytes and a matrix job's `$GITHUB_STEP_SUMMARY` are all unreachable
through the API from this environment (the summary comes back with length 0, observed on job
`114673924412`), that job posts the table — every SHA-256, the sizes and the exact
`gh run download` command — as a comment on the pull request, which *is* readable.

The run id, artefact names and SHA-256 values **for the commit that produces them** live in
that pull-request comment rather than in this file: a document committed in the same push
cannot contain the id of the run the push triggers, and a further commit purely to record it
would move the head away from the commit the artefact was built from.

Every one of those scripts was dry-run locally before being pushed, and the publishing and
verification jobs were mutation-tested against broken fixtures (§8). The dry-runs caught a
`jq`-from-disk read in a job with no checkout, a `case` pattern written `*"-Win64-"` (which
anchors at the end of the string, and would have made the verification job reject every
artefact it exists to check), and a shell quoting bug in the comment step. They did not catch
the interaction with the upstream combine action, which is what Runs 4 and 5 record.

### 9.2 `v0.1.0-beta.2` — previous release (history, unchanged)

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
the loading-defect fix. The same gates now assert `0.1.0-beta.3` / `v0.1.0-beta.3`,
and the standalone job additionally runs the two new ctest checks
(`OptiGDBenchmarkSessionJson`, `OptiGDExternalComparisonTool`). No build target was
removed to obtain a passing build: beta.3 **adds** two targets' worth of sources
(`BenchmarkUI.cpp` to the Geode library, nine new core translation units) and two
ctest checks.

**Third-party review bots**: `CodeRabbit` passed. `Kilo Code Review` reports
`action_required` with the summary *"Review could not start because the account has
insufficient credits"* — an account/billing condition on an external service, not a
finding against this code. No changes were made in response to it.

**Not re-downloaded**: the combined artifact could not be fetched into this
environment (network egress is restricted to `github.com`, `codeload.github.com`,
`api.github.com`, `registry.npmjs.org`, `pypi.org`, `files.pythonhosted.org`; the
artifact lives on `*.blob.core.windows.net`). The verification above therefore
rests on the CI job's own in-archive assertions rather than an independent local
re-inspection of the downloaded zip. This is stated rather than papered over, and it
applies equally to the beta.3 run. CI **logs** are also unreachable from this
sandbox, so beta.3 job status is read from the GitHub API (`gh api`), not from log
inspection.

---

## 10. Honest Status Summary

### 10.1 The thirteen acceptance criteria for `v0.1.0-beta.3`

| # | Criterion | Status | What backs it |
|---|---|---|---|
| 1 | Benchmark Mode reachable in game, with start / timeout / cancel / results | **Mechanism `VERIFIED`; live run `NOT RUN`** | `PauseLayer::customSetup()` button + `BenchmarkPopup` (six views) + controller lifecycle, all unit-tested. No game process exists here, so the click path was never observed. The handoff pass re-audited that path and fixed five defects on it (W, X, Y, Z, UX — `DESIGN_LOG.md` §7.2), including the transition back into active gameplay, which the panel now states explicitly. Popup geometry and the HUD's new bottom-right placement remain unrendered. |
| 2 | Baseline vs optimized via **transient** config that never corrupts saved settings | **`VERIFIED`** | Two-layer `ConfigManager`; base layer is the only one serialized; `testConfigManagerTransientOverrideIsolation` + `testBenchmarkControllerCompletesAndRestoresConfiguration` assert the user config is identical after a capture. |
| 3 | Internally consistent boundaries / samples / counters / percentiles, with regression tests | **`VERIFIED`** | Frame-budget identity, percentile ordering and method disclosure, state derived from data; 12 dedicated tests plus out-of-process validation of the emitted artifact. |
| 4 | Save / reopen / compare / export without overwriting prior results | **`VERIFIED`** | Atomic writes, `already_exists` refusal, id allow-list, no pruning, rebuildable index, export path; 4 tests. |
| 5 | Comparisons flag incompatible sessions instead of manufacturing conclusions | **`VERIFIED`** | Seven blocking conditions, non-blocking warnings, undefined-relative-change handling, `significanceClaimed == false`; 3 tests. |
| 6 | Internal instrumentation clearly distinguished from external presented-frame measurement | **Labels `VERIFIED`; external half `NOT RUN`** | Evidence tags on every artifact, `measurement_definitions`, `limitations`, the overlay footer, `docs/BENCHMARKS.md` §1. No external capture exists to compare against (§5). |
| 7 | Documented Windows x64 CapFrameX procedure + honest test evidence | **Procedure and tool `VERIFIED`; capture `NOT RUN` / `BLOCKED`** | Procedure in-game (**External**) and in `docs/BENCHMARKS.md` §3; `tools/compare_capframex.py` self-tested (ctest). No Windows host, GPU or CapFrameX binary here, so **no external numbers exist** and the CSV fixtures are synthetic. |
| 8 | Discovered bugs investigated, fixed and tested | **`VERIFIED`** | Defects A–U in `DESIGN_LOG.md` §6.3 and §6.7 and W, X, Y, Z plus the panel-guidance gap in §7.2, each with file, root cause, fix and a named regression test. Defect W additionally has an out-of-process guard (`tools/audit_setting_types.py`, two ctest checks) because it is a disagreement between two files that no unit test can see. |
| 9 | Version metadata satisfies Geode's `v0.1.0-beta.3` grammar | **`VERIFIED`** | `tools/validate_geode_metadata.py` (ctest) + `testGeodeVersionMetadataGrammar` + the ported `VersionInfo::parse`; the dot before `3` is mandatory and `v0.1.0-beta3` is asserted rejected. |
| 10 | Win64 / Android64 / Android32 builds still validated | **`[VERIFIED]` — build and package only, for the runs named in §9.1** | Runs `38108365707` (commit `edc0048`) and `38108968555`/`38108967055` (commit `0327231`): all six jobs green (§9.1, Runs 2 and 3), all three Geode targets compiled, linked and packaged, and the combined `.geode` carries all three binaries with `mod.json` asserting `v0.1.0-beta.3`. The first run (`38107167242`, commit `4c686ef`) **failed all three** on defect V. Nothing was executed in game, and no target was removed or disabled to get a green run. The handoff-pass commits are Runs 4–6 (`3587076`, `f3952b2`, `1775194`): all three Geode targets green every time; `combine` **failed** in Runs 4 and 5 because the new named artefacts fed the upstream combine action's `*.geode` glob, and **passed** in Run 6, where the three platform publishing legs then failed on artefacts the upstream action had deleted — causes, evidence and fixes in §9.1. The commit carrying that fix is newer than every run tabulated here, and its own run id, artefact names and SHA-256 values are recorded in the PR conversation, because a file cannot contain the id of the run that the push triggers, and a previous green run does not validate a newer commit. |
| 11 | Docs and `DESIGN_LOG.md` accurate | **`VERIFIED`** | `README.md`, `CHANGELOG.md`, `docs/{ARCHITECTURE,BENCHMARKS,RELEASE_WORKFLOW}.md`, this report; `DESIGN_LOG.md` Entry 6 extended (§6.6–§6.10) and Entry 7 added for the handoff pass, without editing earlier entries. `AGENT.md` untouched: no governance change was authorized, and the standing instruction is to ask first — the question about its stale `v0.1.0-beta.2` version references was put to the maintainer and is recorded as an open decision in §10.3. |
| 12 | Actual results reported; anything not run marked `NOT RUN` / `BLOCKED` | **`VERIFIED`** | This report, `CHANGELOG.md` Notes, `DESIGN_LOG.md` §6.9–§6.10 and `docs/BENCHMARKS.md` all carry the same tags. |
| 13 | No fabricated measurements, FPS gains, benchmark outputs or CI results | **`VERIFIED`** | Every artifact from a standalone binary is stamped `[SIMULATED]` and the code cannot stamp it otherwise (§4.4); the emitted "50.00 → 62.50 FPS" is a declared fixture; CI status is read from the GitHub API, and §9.1 reports the failed run as a failure with its cause rather than presenting only the green one. |

### 10.2 Claims

| Claim | Status |
|---|---|
| `v0.1.0-beta1` failed to load because of the `mod.json` version string | **PASS** (`[VERIFIED]` statically + `[MEASURED]` dynamically) |
| `v0.1.0-beta.3` is accepted by Geode `5.10.1`'s `VersionInfo` grammar | **PASS** (`[MEASURED]` via extracted-grammar harness + ported implementation + validator) |
| `mod.json` / `Types.hpp` / `CMakeLists.txt` / CI version strings are consistent | **PASS** (`[MEASURED]` via `tools/validate_geode_metadata.py`) |
| 51/51 unit & integration tests pass; 8/8 ctest checks; clean under ASan/UBSan; zero compiler warnings | **PASS** (`[MEASURED]` locally) |
| All four JSON/text artifact kinds are schema-valid | **PASS** (`[MEASURED]` — diagnostics report, synthetic benchmark suite, benchmark session set, external-comparison self-test) |
| Every `getSettingValue` / `listenForSettingChanges` type matches the `mod.json` type it reads | **PASS** (`[MEASURED]` by `tools/audit_setting_types.py`, 32 call sites, and mutation-tested against the beta.3 defect) |
| Benchmark Mode can capture, persist, reload, compare and export a session | **PASS** (`[VERIFIED]` by test, `[SIMULATED]` runtime) |
| A benchmark capture cannot corrupt the user's saved settings | **PASS** (`[VERIFIED]`) |
| Prior benchmark evidence is never overwritten or pruned | **PASS** (`[VERIFIED]`) |
| Benchmark Mode fabricates gameplay, forces completion, automates input or alters physics | **NO** — it does none of these, by construction and by test |
| OptiGD sends telemetry anywhere | **NO** — no network code exists in `src/`; all records are local files |
| `mod.json` in the shipped archive declares `v0.1.0-beta.3` | **PASS** (`[VERIFIED]` by CI's hard `jq -e` gates on all three platform archives and the combined archive, run `38108365707`; §9.1) |
| Multi-platform `.geode` builds (Win64 / Android32 / Android64) still succeed | **PASS** (`[VERIFIED]` by CI, run `38108365707`, after defect V was fixed; §9.1). Compile + package only — never loaded by Geode. |
| The mod loads and runs inside a real Geometry Dash `2.2081` client | **BLOCKED** — no GD binary, GPU context, or Geode loader in this environment |
| Benchmark Mode works on screen in a real game (button placement, popup layout, HUD) | **NOT RUN** — derived from the pinned bindings and SDK source; never rendered. This includes the HUD's new bottom-right position: it replaces a placement that would have covered GD's pause button, but neither placement has been looked at in a running game. |
| Any improvement in live in-game frame rate | **NOT CLAIMED / UNVERIFIED** — no in-game capture and no external capture exist. **That the benchmark system works is not evidence that OptiGD improves FPS**, and nothing here should be read as implying it. |
| Statistical significance of any difference | **NOT CLAIMED** — `significanceClaimed` is `false` by construction; a single pair of captures cannot support such a claim |
| A merge, release tag or official release was performed for beta.3 | **NO** — the work stays on `arena/f6290987-optigd` and is handed over as a Draft PR |

### 10.3 Open decisions at the time of writing

| Decision | State | Why it is still open |
|---|---|---|
| Whether to refresh the `v0.1.0-beta.2` version references in `AGENT.md` lines 5–8 to `v0.1.0-beta.3` | **Asked, not decided** | The standing instruction is to ask before touching `AGENT.md` and to make no governance or design-policy change without authorization. Refreshing version strings is arguably not a governance change, but it is the maintainer's file and the question was put to them directly rather than answered on their behalf. `AGENT.md` is therefore unchanged in this PR. If the answer is "yes", it is a documentation-only commit and the artefact for the resulting head must be rebuilt before it is handed over again, so that the file and the commit stay in agreement. |
| Merge of PR #7 | **Not authorized** | The merge gate is: fixes → validated artefact → handoff → the maintainer's real-game result → explicit authorization. The PR stays **Draft**. No merge, no release tag, no official release, and the PR was not closed. |
| Whether the HUD's bottom-right placement is right | **For the maintainer to decide** | It replaces a placement that would have covered GD's pause button. Neither has been rendered; smoke-test item 7 is the check. |

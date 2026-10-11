# Changelog

All notable changes to **OptiGD** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0-beta.3] - 2026-10-11

### Summary
Release candidate that adds a complete **in-game Benchmark Mode** (pause-menu entry point, transient baseline/optimized captures, persisted session records, comparison, export), repairs the telemetry-integrity defects found while building it, and locks both behind regression tests. Test suite grows from 20 to **50** test functions; `ctest` from 4 to **6** checks.

No live-game performance claim is made or implied by any of this: the benchmark system working is not evidence that OptiGD improves frame rate, and every artifact in this repository that came from a standalone binary is stamped `[SIMULATED]`.

### Added
- **Benchmark Mode core (`src/benchmark/*`, ~2,900 lines)**:
  - `BenchmarkTypes` — modes, states, statuses, a fixed integrity-warning vocabulary, best-effort `LevelIdentity` (every field carries an explicit "known" flag), environment metadata, and the evidence-classification tags.
  - `BenchmarkSession` — the record: measurement window, statistics, the configuration **actually in force**, integrity counters, warnings, measurement definitions and limitations. JSON round-trip gated on `report_kind` + `schema_version`; a newer schema is refused rather than reinterpreted.
  - `BenchmarkStore` — atomic writes (temp + rename), id allocation by probing the directory, an allow-list so an id can never escape the store directory, tolerant listing (a corrupt file is reported as unreadable, not dropped), a rebuildable `index.json`, **no overwrite of prior evidence** and **no pruning**.
  - `BenchmarkController` — lifecycle state machine: start / frame routing / pause + stall exclusion / reset / exit / cancel / auto-finalize, a verified transient config override re-checked every 16th frame, and persist with one retry under a fresh id.
  - `BenchmarkCompare` — per-metric direction against a ±2 % noise threshold, absolute and relative deltas, compatibility gating, text rendering. `significanceClaimed` is `false` by construction.
- **`src/ui/BenchmarkPanel.{hpp,cpp}`**: every string the user reads (HUD, main, results, detail, comparison, protocol, external procedure) plus pagination — platform-agnostic, so the Geode layer adds no measurement logic.
- **`src/geode/BenchmarkUI.{hpp,cpp}`**: a 460×296 `geode::Popup` with six views (Main / Results / Detail / Compare / Protocol / External), an **OptiGD Benchmark** button added in `PauseLayer::customSetup()`, the runtime bridge, and notifications on terminal states.
- **`src/diagnostics/SampleWindow.{hpp,cpp}`**: bounded measurement window (fixed-capacity ring + fixed-bin histogram + exact accumulators), O(1) push, and it **states** whether its percentiles are `exact` or `histogram_approximate`. Owns the single percentile definition used by the governor, the diagnostics engine and Benchmark Mode.
- **`src/core/JsonValue.{hpp,cpp}`**: minimal JSON reader/writer so a saved session can be re-opened in-game without Python. Non-finite doubles serialize as `null` and set a `sawNonFinite` flag.
- **Two new mod settings**: `benchmark-duration-seconds` (15–600, default 60) and `benchmark-stop-on-death` (default `true`).
- **`tools/validate_benchmark_session_json.py`** + **`cmake/EmitBenchmarkSessions.cmake`**: `optigd_tests --emit-sessions <dir>` drives the real controller with a synthetic clock into a temporary directory, and the validator checks structure **and** cross-field invariants (frame budget identity, percentile ordering, `avg_fps` vs. mean frame time, `captured ≤ wall`, state vs. data, baseline passthrough disclosure, `override_applied`, warning vocabulary, index/session agreement, evidence classification). Verified against 13 hand-tampered artifacts: all 13 detected.
- **`tools/compare_capframex.py`**: side-by-side comparison of external CapFrameX CSV captures (Windows x64), using the same percentile definition as the internal statistics, reporting the 1 % low FPS and the run-to-run spread, and refusing to read a file it cannot interpret. Includes `--self-test` on synthetic fixtures (ctest `OptiGDExternalComparisonTool`).
- **`ConfigManager` transient override layer**: `m_config` (base, the only layer ever serialized) and `m_override` (transient), with `setRuntimeOverride` / `updateRuntimeOverride` / `clearRuntimeOverride`. A benchmark capture cannot corrupt the user's saved settings.
- **`Platform`**: architecture string and thread CPU time (`CLOCK_THREAD_CPUTIME_ID` / `GetThreadTimes`).

### Fixed
- **Every stall ≥ 250 ms disappeared from the telemetry**: the frame delta was accepted only when `100 < us < 250000`, otherwise beta.2 substituted GD's own clamped `dt * 1e6` — a plausible frame time in place of the exact frames that make a level feel bad. The raw monotonic interval is now measured, classified against `core::kStallThresholdMs`, recorded as a gap and excluded from the window (`Hooks.cpp`, `OptiGDManager::endFrame`).
- **A NaN metric was written as a plausible `0`**: `json::Value::asDouble(fallback)` returned the fallback for a non-finite stored double, so the serializer wrote `0` instead of `null` and `sawNonFinite` never fired. `asDouble` now returns the stored value exactly; sanitizing stays the writer's job.
- **Session records claimed `[MEASURED-IN-GAME]` no matter who wrote them**: the classification was hardcoded in `toJson()`, so artifacts emitted by the standalone test binary asserted they had been measured inside a live Geometry Dash process. It is now a field stamped by the controller from the runtime it is attached to (`setLiveGameRuntime`, true only in the Geode bridge), defaults to `[SIMULATED]`, and loading a file may only **downgrade** it.
- **A refused override still destroyed the override**: `ConfigManager::setRuntimeOverride` rewrote `m_override` even when returning `false`, so a second session wiped the configuration the first one needed restored. It now returns early without touching state.
- **The controller's re-apply path was dead code**: `verifyOverride()` "re-installed" the override through a call that refuses while one is armed, so a session that noticed a clobbered configuration could never restore its own values — or the user's. The bridge gained `updateOverride`, used by both the take-over and re-apply paths.
- **A hand-edited record could claim more than its data supported**: `BenchmarkSession::fromJson` never re-derived counters or warnings, so a `completed` session with zero samples loaded as comparable. Loading now calls `refreshDerivedWarnings()`.
- **A user cancellation was relabelled `invalid`**: the terminal state is derived from the data, which is right for a capture that ended on its own, but it hid the one fact the user knows. `cancelled` is now preserved (still non-comparable, still carrying `no_samples_recorded` / `capture_shorter_than_target`); `invalid` is reserved for captures with too little data that were not explicitly cancelled.
- **Two different "p95" definitions in one codebase** (audit finding J): `AdaptiveGovernor` indexed percentiles at `floor(n * p)` while the diagnostics window used nearest-rank. They differ by exactly one sample whenever `n * p` is an integer — the common case for a round window, where the governor's "p95" was the *slowest* frame in it. Both now call `SampleWindow::medianFromSorted` / `percentileFromSorted`.
- **Telemetry hygiene** (audit findings A–I): the diagnostics window now discloses its sample count, window and duration (`report_schema_version: 2`) instead of reporting `total_frames` from an unbounded counter while percentiles came from ≤ 3600 samples; `DiagnosticsEngine::reset()` is level-scoped; telemetry is gated on an active `PlayLayer` (frames recorded in `LevelEditorLayer` are not gameplay); the fabricated `culledParticles = emitters * 32` estimate is gone; the `uint64 → uint32` matrix-cache delta truncation is gone; `updateThreadCpuUs` is reported separately from the wall-clock `updateCpuTimeUs`; subsystem time recorded outside an open frame window is counted separately instead of being attributed to the next frame; the diagnostics export is atomic and checks its stream state; and non-finite values are sanitized with an explicit note instead of emitting unparseable `nan` / `inf`.
- **Diagnostics overlay labels**: `Update CPU:` implied thread CPU time for a wall-clock span. It now reads `Update: X ms wall / Y ms thread`, and the overlay states that its numbers are internal telemetry from a 120-frame governor window, **not** presented-frame or GPU timing.

### Changed
- `tests/UnitTests.cpp` grows from 20 to 50 test functions; the 30 new ones lock the benchmark invariants and the telemetry-integrity fixes behind regressions.
- `ctest` grows from 4 to 6 checks (`OptiGDBenchmarkSessionJson`, `OptiGDExternalComparisonTool`).
- `optigd_diagnostics.json` gains `report_schema_version: 2`, `paused_frames` and window-disclosure keys. Every beta.2 key is retained; a file without `report_schema_version` is v1 and still validates.
- Version strings (`mod.json`, `Types.hpp`, `CMakeLists.txt`, CI `jq -e` gates, tools, tests) move to `v0.1.0-beta.3` / `0.1.0-beta.3`.

### Notes
- Live in-game Benchmark Mode runs, live frame-time captures, and the CapFrameX external procedure remain **`[NOT RUN] / [BLOCKED]`**: this environment has no Geometry Dash binary, no GPU/display, no Geode loader, no Windows host and no Android device. `src/geode/*` compiles only in CI.
- No merge, no release tag and no official release were performed for beta.3.
- Stale Draft PR #4 (`arena/21ca20f2-optigd`) is untouched and superseded by merged PRs #5 and #6.

## [0.1.0-beta.2] - 2026-10-10

### Summary
Release candidate that fixes the blocking loader defect in `v0.1.0-beta1` (the mod was skipped at startup with `unexpected type string`), closes six audit findings against the core engine, and adds out-of-process schema validation for both JSON artefacts the mod emits.

### Fixed
- **Blocking loader defect (root cause)**: `"version": "v0.1.0-beta1"` in `mod.json` is rejected by Geode `5.10.1`'s `VersionInfo::parse` (`Expected end of version, found '1'`), which `JsonValidation::tryGet<T>()` reports as `unexpected type string` and `LoaderImpl` turns into `addProblem({InvalidGeodeFile})`. The version is now `v0.1.0-beta.2` — the only prerelease form Geode accepts that preserves the `beta2` designation. See `EVIDENCE_REPORT.md` for the static and dynamic evidence chain.
- **`VisibilityCuller` visibility-state desync**: `encodeIndex`/`decodeIndex` now offset by `+1` so object index `0` is distinguishable from the `SpatialHash`'s "no item" sentinel. Without this, object 0's visibility could never be tracked.
- **`VisibilityCuller::setCellSize()` mid-level**: now calls a new private `rebuildSpatialIndex()`; previously the cell-size change applied to newly inserted objects only, leaving the existing index on the old grid.
- **Culling could not be undone**: added `restoreAllVisibility()` (plus `hasCulledObjects()` / `getCellSize()`). `PlayLayer` now tracks `cullingEngaged` and restores a captured buffer when `enableSpatialCulling` is switched off mid-level, so previously hidden decorative objects reappear instead of staying invisible for the rest of the attempt.
- **`ParticleGovernor` derived stats were `O(N)` per call**: `getLastAvoidedParticleCount()` is now lazy (`recomputeDerivedStats()` behind a dirty flag), and `m_currentTotalParticles` / `m_sleepingEmitterCount` are maintained incrementally in `recordActiveParticles()`, `unregisterEmitter()`, and the sleep/wake transitions. `recalculateParticleTotals()` is gone.
- **`MatrixCache` served matrices it never computed**: entries created implicitly by `setParent()` were indistinguishable from computed ones, so a default-initialised identity matrix could satisfy a `getCached()` lookup. Added a `computed` flag, made `store()` anchor-aware so it cannot desync from `getOrCompute()`'s cache key, and added `pruneStaleEntries()` to drop never-computed phantom nodes (with child detachment, so no dangling pointers).
- **`FallbackManager` recovery was unreachable during cooldown**: recovery is now evaluated every frame; the cooldown only gates the application of a *new* fallback. Previously a stabilised workload could not restore its pre-fallback settings until the whole cooldown elapsed.
- **`CompatibilityLayer` GD gate was a prefix test**: `starts_with("2.2")` accepts GD `2.21x`, whose structure layouts differ from the `2.2081` bindings OptiGD is compiled against. Replaced with a numeric comparison against `kTargetGDVersion`.
- **Tier defaults were duplicated**: `ConfigManager::applyTierPresetUnlocked()` and `AdaptiveGovernor::evaluateTierAdjustment()` carried independent copies, so an automatic transition could apply different settings than the same tier selected by hand. Both now use `core::kTierDefaults` / `core::applyTierDefaults()` in `Types.hpp`; `Custom` is an explicit no-op.
- **`TriggerOptimizer::inspectLookaheadWindow()` mutated execution state** despite being documented as non-destructive. It now uses a separate `reportedInLookahead` flag and leaves `activated` / `isCompleted` untouched, so telemetry cannot perturb what `evaluate()` would execute.
- **`Profiler::record()` took a mutex and materialised a `std::string` key on the hot path** — a per-frame heap allocation that contradicted the zero-allocation claim. Added `MetricId` + `recordById()` (fixed `std::array`, no lock, no allocation); the name-based API resolves known metrics to the same slots.
- **Robustness**: `ConfigManager::deserialize` no longer throws on malformed persisted text (guarded `parseInt`/`parseFloat`/`parseUnsigned`); `MemoryArena` no longer destroys live frame-scoped data when reusing an existing block; `DiagnosticsEngine` JSON now escapes every interpolated string; `#pragma comment(lib,"psapi.lib")` is `_MSC_VER`-guarded.

### Added
- **`src/core/GeodeVersion.{hpp,cpp}`**: a faithful port of Geode `5.10.1`'s `VersionInfo::parse` / `VersionTag::parse`, so the exact grammar is testable without the SDK.
- **`tools/validate_geode_metadata.py`**: validates `mod.json` against the SDK's mod-id regex, `gd` object shape, `VersionInfo` grammar and settings-schema rules, and cross-checks `version` / `geode` / `gd` against `Types.hpp` and `CMakeLists.txt`. Runs in `ctest`.
- **`tools/validate_diagnostics_json.py`** and **`tools/validate_benchmark_json.py`**: schema validators for the two JSON artefacts. The benchmark validator additionally asserts `evidence_classification == "[SIMULATED]"` and `optigd_heap_allocations == 0`.
- **`optigd_tests --emit-report <path>`**: writes a real diagnostics JSON report so the artefact can be validated out of process (wired up as the `OptiGDDiagnosticsJson` ctest).
- **Per-subsystem timing**: `recordCullingTimeUs` / `recordParticleTimeUs` / `recordTriggerTimeUs` / `recordMatrixTimeUs` on `OptiGDManager`, fed from the hooks.
- **Honest trigger telemetry**: `DiagnosticsEngine` now emits a `notes` block stating `native_trigger_execution_modified: false` and defining `total_triggers_skipped`, and trigger counters report `0` unless `enableTriggerIndexing` is on — indexing is telemetry-only and never gates native execution.

### Changed
- Trigger counters are reported as `0` unless trigger indexing is enabled (previously they could imply a saving that was never realised).
- `tests/UnitTests.cpp` grows from 9 to 20 tests; the eleven new ones lock the audit fixes behind regressions.

### Notes
- Live in-game frame-time captures on physical Windows/Android hardware remain **`[UNVERIFIED / BLOCKED]`**. No live-game performance gain is claimed.

## [0.1.0-beta1] - 2026-10-10

### Summary
First validated beta candidate (`v0.1.0-beta1`) of OptiGD targeting Geometry Dash `2.2081` and Geode SDK `v5.10.1` across **Windows x64 (`Win64`)**, **Android 64-bit (`Android64`)**, and **Android 32-bit (`Android32`)**.

### Added
- **Cross-Platform Geode Hooks (`src/geode/Hooks.cpp`)**:
  - Enabled `PlayLayer`, `GJBaseGameLayer`, and `cocos2d::CCParticleSystem` hooks across Windows x64, Android 32-bit, and Android 64-bit (removed Windows-only `#ifdef GEODE_IS_WINDOWS` guard on gameplay hooks).
  - Hooked `GJBaseGameLayer::updateObjectSection`, `removeObjectFromSection`, `spawnGroup`, and `toggleGroup` to keep `SpatialHash`, `VisibilityCuller`, `MatrixCache`, and `TriggerOptimizer` synchronized with dynamic gameplay state.
  - Added `Custom` performance profile settings (`custom-particle-density`, `custom-max-particles`, `custom-cull-offscreen-particles`, `custom-camera-padding-x`, `custom-camera-padding-y`, `target-fps`) to `mod.json`.
- **Differential Frustum Culling & Arena Queries (`VisibilityCuller`, `SpatialHash`)**:
  - Added `SpatialHash::queryArena()` and `queryIDsArena()` backed by `MemoryArena` for zero-heap-allocation spatial queries.
  - Added `VisibilityCuller::evaluateVisibilityDelta()` to compute `newlyVisible` and `newlyCulled` decorative objects in $O(k)$ rather than scanning all $N$ objects.
- **Comparative Telemetry Tooling (`DiagnosticsEngine`, `tools/generate_report.py`)**:
  - Added median (`P50`), `P95`, `P99`, `avg_update_cpu_time_ms`, and `avg_optigd_overhead_us` metrics to `DiagnosticsEngine` JSON reports.
  - Added `python3 tools/generate_report.py --compare <baseline.json> <optigd.json>` for before/after real-game telemetry analysis.
- **Project Governance & Audit Trail**:
  - Added `AGENT.md` and `DESIGN_LOG.md`, and updated `EVIDENCE_REPORT.md` with a module-by-module verification table.

### Fixed
- **Version Metadata Alignment**: Updated version from premature `v1.0.0` to `v0.1.0-beta1` across `mod.json`, `CMakeLists.txt` (`PROJECT(OptiGD VERSION 0.1.0)` + `OPTIGD_VERSION="0.1.0-beta1"`), `Types.hpp`, `DiagnosticsEngine`, `DiagnosticsOverlay`, `BenchmarkSuite`, and documentation.
- **Spatial Culling Hook Toggle (`src/geode/Hooks.cpp`)**: Fixed `PlayLayer::updateVisibility(float dt)` where both branches of `enableSpatialCulling` previously executed identical no-op passthrough calls.
- **Moving Object Re-Indexing (`src/core/SpatialHash.cpp`)**: Fixed `SpatialHash::update()` so moving an object across grid cell boundaries removes old cell bucket entries and inserts new cell bucket entries, preventing stale spatial queries.
- **Preset Synchronization Overwrite (`src/config/ConfigManager.cpp`, `src/geode/Hooks.cpp`)**: Fixed `syncSettingsWithConfig()` where applying a tier preset was previously overwritten by writing back a stale pre-preset `OptiConfig` copy.
- **Particle Emission Decay & Finite-Duration Safety (`src/optimization/ParticleGovernor.cpp`)**: Stored each emitter's `baseEmissionRate` to prevent frame-over-frame compounding emission decay, preserved finite-duration and `autoRemoveOnFinish` emitters offscreen, and clamped `dt` on wake.
- **Trigger Semantics & Rewind (`src/optimization/TriggerOptimizer.cpp`)**: Added `TouchOnly` hitbox-intersection evaluation, `isMultiTriggered` re-activation, deterministic `(triggerX, spawnOrder, id)` sorting, group toggle state (`setGroupEnabled`), and backward movement / level reset cursor rewind.
- **MatrixCache Dirty Invalidation (`src/optimization/MatrixCache.cpp`)**: Fixed `getCached()` to reject entries marked `dirty`, added parent-to-child dirty propagation (`setParent`), and added pointer eviction (`evict`).
- **Geode Version Reporting (`src/core/OptiGDManager.cpp`, `src/geode/Hooks.cpp`)**: Passed live Geode loader version (`v5.10.1`) to `CompatibilityLayer::initialize()` instead of hardcoded `"v4.0.0"`.
- **Honest MemoryArena & Heap Allocation Accounting (`src/core/OptiGDManager.cpp`)**: Connected `MemoryArena` to `VisibilityCuller` and `AdaptiveGovernor` per-frame scratch paths and separated `arenaAllocations` from `heapAllocationsAvoided`.
- **CI Artifact Verification (`.github/workflows/build.yml`, `release.yml`)**: Switched `geode-sdk/build-geode-mod` to `sdk: given` and added `unzip` + `jq` verification checks asserting `coderunknow.optigd.dll`, `coderunknow.optigd.android64.so`, `coderunknow.optigd.android32.so`, and `mod.json` (`v0.1.0-beta1`) inside built and combined `.geode` archives.

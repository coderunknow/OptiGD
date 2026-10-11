# OptiGD `v0.1.0-beta.3` Architecture Specification

## 1. High-Level Overview

OptiGD is designed around a strict separation between:
1. **The Platform-Agnostic C++20 Core Engine (`optigd_core`)**: Contains all spatial data structures, particle/trigger/matrix optimization algorithms, adaptive frame-time governance, configuration management, local telemetry, and — new in beta.3 — the whole of Benchmark Mode (`src/benchmark/*`, `src/ui/BenchmarkPanel.*`, `src/diagnostics/SampleWindow.*`, `src/core/JsonValue.*`). Compiles standalone on Linux, Windows, and Android with zero proprietary dependencies, enabling deterministic unit testing (`optigd_tests`) and synthetic stress benchmarking (`optigd_benchmark`). Every measurement rule lives here, so it is testable without a game.
2. **The Cross-Platform Geode Bridge (`src/geode/Hooks.cpp` + `src/geode/BenchmarkUI.cpp`)**: Connects `optigd_core` to Geometry Dash `2.2081` via Geode SDK `v5.10.1` `$modify` hooks on `GJBaseGameLayer`, `PlayLayer`, `PauseLayer`, and `cocos2d::CCParticleSystem` across **Windows x64 (`Win64`)**, **Android 64-bit (`Android64`)**, and **Android 32-bit (`Android32`)**. The bridge is deliberately thin: it reads game state, feeds frames, and renders text the core produced.

---

## 2. Frame Execution Lifecycle

During active gameplay inside `PlayLayer`:

1. **`GJBaseGameLayer::update(float dt)`**:
   - Computes the wall-clock inter-frame interval (`interFrameUs`) since the previous frame from a monotonic clock and calls `OptiGDManager::beginFrame()` + `setExternalFrameDeltaUs(interFrameUs)`. The interval is **not** clamped or replaced by GD's own `dt`: beta.2 substituted `dt * 1e6` whenever the measured interval fell outside `100 < us < 250000`, which hid every stall ≥ 250 ms behind a plausible frame time. The raw interval is now measured, classified against `core::kStallThresholdMs`, and a stall is recorded as a gap instead of being counted as gameplay.
   - Detects the pause edge (`PlayLayer::m_isPaused`) and calls `OptiGDManager::setFramePaused(bool)`, because a paused frame is indistinguishable from a played one by interval alone: with the pause menu open, `update` still runs at the display rate.
   - `beginFrame()` resets the per-frame `MemoryArena` in $O(1)$ by rewinding block offsets to `0`.
   - Executes `GJBaseGameLayer::update(dt)` (which runs player physics, collision checks, trigger processing, `PlayLayer::updateVisibility(dt)`, and `PlayLayer::postUpdate(dt)`).
   - Calls `OptiGDManager::endFrame()`, which records `updateCpuTimeUs` (the **wall** span of the `update` call) and `updateThreadCpuUs` (thread CPU time for the same call) separately from `totalFrameTimeUs` (frame-to-frame interval), updates `AdaptiveGovernor` using `MemoryArena` scratch memory, checks `FallbackManager` safety budgets, writes back tier changes only when the tier actually changed, and records frame telemetry into `DiagnosticsEngine` and into the running benchmark session.
   - Per-subsystem spans recorded outside an open frame window are counted separately (`getOutOfWindowSubsystemEvents()` / `getOutOfWindowSubsystemTimeUs()`) rather than being attributed to the next frame.

2. **`PlayLayer::updateVisibility(float dt)`**:
   - Always runs native `PlayLayer::updateVisibility(dt)` first so Geometry Dash's native section activation, enter/fade effects, and gameplay object states remain 100% preserved.
   - When `enableSpatialCulling` is active, computes the world-space camera frustum (`computeCameraWorldRect`) and calls `VisibilityCuller::evaluateVisibilityDelta(cameraRect, frameArena, &newlyVisibleDeco, &newlyCulledDeco)`.
   - `VisibilityCuller` queries `SpatialHash::queryIDsArena()` into the per-frame `MemoryArena` (avoiding heap allocations) and compares generational frame stamps against `m_previouslyVisibleIndices` in $O(k)$ visible objects.
   - Decorative objects (`(m_isDecoration || m_isDecoration2) && !m_isTrigger`) that left the padded frustum are hidden (`setVisible(false)`), and decorative objects that entered the padded frustum (and do not have `m_isHide`, `m_isInvisible`, or `m_isDisabled` set) are shown (`setVisible(true)`).

3. **`GJBaseGameLayer::updateObjectSection(GameObject* object)` & `removeObjectFromSection(GameObject* object)`**:
   - When moving objects cross section boundaries, `updateObjectSection` marks the object dirty in `MatrixCache` and updates its bounding box in `VisibilityCuller::updateObjectBoundsByPtr(object, bounds)`, which re-indexes the object's cell range in `SpatialHash::update()`.
   - When an object is removed from a section, `removeObjectFromSection` evicts its pointer from `MatrixCache::evict(object)`.

4. **`cocos2d::CCParticleSystem::update(float dt)`**:
   - When `PlayLayer::get()` is active and `enableParticleThrottling` is enabled, `ParticleGovernor::evaluateEmitterDetailed()` evaluates each emitter:
     - **`Critical` Emitters** (attached to `m_player1` or `m_player2`): Always update at `1.0x` emission rate, even off-screen.
     - **Finite-Duration / Auto-Remove Emitters** (`m_fDuration >= 0` or `m_bIsAutoRemoveOnFinish`): Never sleep off-screen so burst timers complete and auto-remove cleanly.
     - **Continuous Off-Screen Emitters**: Put to sleep when outside the padded camera frustum (`m_uParticleCount = 0; m_fEmitCounter = 0.0f` on sleep entry). On wake, `dt` is clamped to `<= 1/60s` and `m_fEmitCounter` is reset to prevent emission bursts.
     - **Non-Compounding Density Scaling**: Scales `m_fEmissionRate = baseEmissionRate * emissionMultiplier` from the emitter's initial base rate so emission never decays across frames.

---

## 3. Subsystem Details

### 3.1 `MemoryArena` (`src/core/MemoryArena.*`)
- Multi-block linear bump allocator with power-of-two alignment normalization (`normalizeAlignment`), zero-byte allocation guard (`allocate(0) -> nullptr`), and integer overflow protection.
- Eliminates per-frame heap allocations in `SpatialHash::queryIDsArena()` and `AdaptiveGovernor::calculateWindowStats()`.

### 3.2 `SpatialHash` (`src/core/SpatialHash.*`)
- 2D uniform grid mapped via open-addressed power-of-two bucket table (`(ux * 73856093) ^ (uy * 19349663)`).
- Stores each item's covered cell range (`minCellX..maxCellX`, `minCellY..maxCellY`).
- `update(id, newBounds)` diffs the old and new cell ranges, removing the item only from cells it left and inserting it only into cells it entered.

### 3.3 `TriggerOptimizer` (`src/optimization/TriggerOptimizer.*`)
- Supports four trigger execution models: `TouchOrPosition` (X-coordinate crossing), `TouchOnly` (2D player hitbox intersection), `Spawn` ($O(1)$ group ID bucket lookup), and `Continuous` (per-frame).
- Supports `isMultiTriggered`, deterministic `(triggerX, spawnOrder, id)` sorting, group enable/disable (`setGroupEnabled`), and automatic cursor rewind on reverse player movement or `resetLevel()`.

### 3.4 `MatrixCache` (`src/optimization/MatrixCache.*`)
- Stores `AffineMatrix` (`a, b, c, d, tx, ty`) with explicit `dirty` flag, `dirtyGeneration` counter, and parent-child dependency links (`setParent`).
- Calling `markDirty(parent)` recursively marks all registered child nodes dirty; `getCached()` rejects any entry where `dirty == true` or whose parent is dirty.

### 3.5 `AdaptiveGovernor` & `FallbackManager` (`src/adaptive/*`)
- `AdaptiveGovernor` evaluates a 120-frame rolling window of frame times (`meanMs`, `medianMs`, `p95Ms`, `p99Ms`, `stdDevMs`) and transitions between `HighEnd`, `Balanced`, and `LowEnd` with a 120-frame hysteresis cooldown. Its percentiles come from `SampleWindow::medianFromSorted` / `percentileFromSorted` — the same definition the diagnostics window and Benchmark Mode use — so one machine can never produce two different "p95" numbers.
- `FallbackManager` monitors both frame-time jitter (`stdDevMs > threshold`) and per-subsystem CPU budgets (`matrixTimeUs`, `triggerTimeUs`). When triggered, it snapshots `m_preFallbackConfig`, applies safe downgrades, and restores `m_preFallbackConfig` once frame times stabilize.

---

## 4. Measurement Layer (new in `v0.1.0-beta.3`)

### 4.1 `SampleWindow` (`src/diagnostics/SampleWindow.*`)
- A bounded measurement window: fixed-capacity ring of frame times + fixed-bin histogram + exact accumulators. `push()` is O(1) with no per-sample heap churn.
- **States how its percentiles were produced.** `exact` while the ring holds every sample; `histogram_approximate` after an overflow, with `ringOverflow` / `histogramSaturated` flags. Approximate values are rendered with a `~` and raise the `percentiles_approximated` warning.
- Owns the single percentile definition: P50 = median (mean of the two central values for even `n`); P95/P99 = nearest-rank (`rank = ceil(p * n)`, `index = rank - 1`).
- Distinguishes three outcomes for an offered frame: counted, excluded (`recordExcluded`, for stalls and pauses — time that is reported but not averaged in), and rejected (non-finite or non-positive interval).

### 4.2 Two-layer configuration (`src/config/ConfigManager.*`)
- `m_config` is the **base** layer: the only layer written by `syncFromSettings()`, `deserialize()`, `resetToDefaults()`, `applyTierPreset()` or `setTargetFrameRate()`, and the only layer ever serialized. It is the user's saved configuration.
- `m_override` is the **transient** layer used by Benchmark Mode. `getConfig()` returns the override while one is armed; `setConfig()` writes whichever layer is active.
- `setRuntimeOverride()` refuses (returning `false` and touching nothing) when an override is already armed, so an overlapping session cannot silently replace a configuration someone else needs restored. `updateRuntimeOverride()` re-asserts the configuration of an override the caller already owns. `clearRuntimeOverride()` refuses when nothing is armed.
- Consequence: a benchmark capture cannot corrupt saved settings, and the engine's own tier write-back (`endFrame()` on a tier change) cannot clobber a capture's verified configuration without the controller noticing.

### 4.3 Benchmark Mode (`src/benchmark/*`, `src/ui/BenchmarkPanel.*`)
- `BenchmarkTypes` — modes, states, statuses, the fixed integrity-warning vocabulary, best-effort `LevelIdentity` (every field carries an explicit "known" flag), environment, and the evidence-classification tags.
- `BenchmarkSession` — the record: window, statistics, the configuration **actually in force**, integrity counters, warnings, measurement definitions and limitations. JSON round-trip gated on `report_kind` and `schema_version`; a record newer than the build is refused rather than reinterpreted, and loading re-derives counters and warnings so a hand-edited file cannot claim more than its data supports.
- `BenchmarkStore` — atomic writes (temp + rename), id allocation by probing the directory, an allow-list on ids so an id can never escape the store directory, tolerant listing (a corrupt file is reported as unreadable, not dropped), a rebuildable `index.json`, no overwrite of prior evidence and no pruning.
- `BenchmarkController` — the lifecycle state machine: start / frame routing / pause and stall exclusion / reset / exit / cancel / auto-finalize, a verified transient override re-checked every 16th frame, and persist-with-one-retry-under-a-fresh-id. The terminal state is derived from the data, not from the caller's request.
- `BenchmarkCompare` — direction per metric against a ±2 % noise threshold, absolute and relative deltas, compatibility gating that blocks rather than concludes, `significanceClaimed == false` by construction.
- `ui::BenchmarkPanel` — every string the user reads (HUD, main, results, detail, comparison, protocol, external procedure) plus pagination. Platform-agnostic, so the Geode layer adds no measurement logic.
- `geode::BenchmarkPopup` — a 460×296 `geode::Popup` with six views, opened from an **OptiGD Benchmark** button added in `PauseLayer::customSetup()`. Text is refreshed at most every 0.25 s while a capture runs.

### 4.4 Evidence classification
Every artifact states where its numbers came from, in a fixed vocabulary
(`benchmark::evidence`): `[MEASURED-IN-GAME]` for a capture inside a live GD
process, `[SIMULATED]` for anything a standalone binary produced, and
`[INTERNAL-TELEMETRY]` for the legacy diagnostics report. The tag is stamped by the
code that produced the record (`BenchmarkController::setLiveGameRuntime`, set only
by the Geode bridge), it defaults to the conservative value, and reading a file may
only downgrade it. See `docs/BENCHMARKS.md`.

# OptiGD `v0.1.0-beta.2` Architecture Specification

## 1. High-Level Overview

OptiGD is designed around a strict separation between:
1. **The Platform-Agnostic C++20 Core Engine (`optigd_core`)**: Contains all spatial data structures, particle/trigger/matrix optimization algorithms, adaptive frame-time governance, configuration management, and local telemetry. Compiles standalone on Linux, Windows, and Android with zero proprietary dependencies, enabling deterministic unit testing (`optigd_tests`) and synthetic stress benchmarking (`optigd_benchmark`).
2. **The Cross-Platform Geode Bridge (`src/geode/Hooks.cpp`)**: Connects `optigd_core` to Geometry Dash `2.2081` via Geode SDK `v5.10.1` `$modify` hooks on `GJBaseGameLayer`, `PlayLayer`, and `cocos2d::CCParticleSystem` across **Windows x64 (`Win64`)**, **Android 64-bit (`Android64`)**, and **Android 32-bit (`Android32`)**.

---

## 2. Frame Execution Lifecycle

During active gameplay inside `PlayLayer`:

1. **`GJBaseGameLayer::update(float dt)`**:
   - Computes the wall-clock inter-frame interval (`interFrameUs`) since the previous frame (with fallback to `dt * 1e6`) and calls `OptiGDManager::beginFrame()` + `setExternalFrameDeltaUs(interFrameUs)`.
   - `beginFrame()` resets the per-frame `MemoryArena` in $O(1)$ by rewinding block offsets to `0`.
   - Executes `GJBaseGameLayer::update(dt)` (which runs player physics, collision checks, trigger processing, `PlayLayer::updateVisibility(dt)`, and `PlayLayer::postUpdate(dt)`).
   - Calls `OptiGDManager::endFrame()`, which records `updateCpuTimeUs` (CPU time spent inside `update`) separately from `totalFrameTimeUs` (frame-to-frame interval), updates `AdaptiveGovernor` using `MemoryArena` scratch memory, checks `FallbackManager` safety budgets, and records frame telemetry into `DiagnosticsEngine`.

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
- `AdaptiveGovernor` evaluates a 120-frame rolling window of frame times (`meanMs`, `medianMs`, `p95Ms`, `p99Ms`, `stdDevMs`) and transitions between `HighEnd`, `Balanced`, and `LowEnd` with a 120-frame hysteresis cooldown.
- `FallbackManager` monitors both frame-time jitter (`stdDevMs > threshold`) and per-subsystem CPU budgets (`matrixTimeUs`, `triggerTimeUs`). When triggered, it snapshots `m_preFallbackConfig`, applies safe downgrades, and restores `m_preFallbackConfig` once frame times stabilize.

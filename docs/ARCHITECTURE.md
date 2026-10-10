# OptiGD v0.1.0-beta1 — Architecture

This document describes what the code does in this release. Where a module is not connected to
gameplay, it says so. Status labels follow `EVIDENCE_REPORT.md`.

## 1. Layers

```
Geometry Dash 2.2081 (PlayLayer)
        │  Geode $modify hooks (src/geode/Hooks.cpp)
        ▼
OptiGDManager (src/core)  ── owns ──► user config snapshot (ConfigManager)
        │                              effective config (adaptive + fallback)
        ├─► frame timing: frame interval, game update time, OptiGD overhead
        ├─► VisibilityCuller (shadow measurement only)  ─► SpatialHash
        ├─► AdaptiveGovernor ─► FallbackManager          (effective tier/levels)
        ├─► ParticleGovernor   (DISABLED: not connected)
        ├─► TriggerOptimizer   (DISABLED: model only)
        └─► DiagnosticsEngine ─► FrameTimeRecorder (CSV, setting-gated)
```

The core (`src/core`, `src/optimization`, `src/adaptive`, `src/config`, `src/diagnostics`,
`src/profiling`, `src/compatibility`, `src/platform`) has no Geode dependency. It is built and tested
natively (`optigd_core`, `optigd_tests`). Only `src/geode/Hooks.cpp` depends on Geode.

## 2. Frame lifecycle (what the hooks do)

| Hook | Action |
|---|---|
| `$on_mod(Loaded)` | `OptiGDManager::initialize`, `syncSettings`, settings listener |
| `PlayLayer::init` | `onLevelLoaded`; registers level objects **only if** `enable-culling` is ON |
| `PlayLayer::resetLevel` | Same as init (level restart) |
| `PlayLayer::onQuit` | `onLevelExited`; clears shadow entries |
| `PlayLayer::postUpdate` | `beginFrame` → `markGameUpdateBegin` → original → `markGameUpdateEnd` → shadow pass (if enabled) → `endFrame` → CSV row (if enabled) |
| `MenuLayer::init` | Log line only |

`beginFrame` measures the frame interval as the time since the previous `beginFrame`. The first frame
after a level load has interval 0 and is not a sample.

## 3. Modules

### 3.1 Core
- **`MemoryArena`**: bump allocator. Validates alignment (power of two, non-zero) and size (overflow-safe,
  1 GiB block guard). `reset()` is O(1) and keeps memory. Counters are honest: `getAllocationCount()`
  counts successful allocations; `getHeapBlockAllocations()` counts block allocations from the heap.
- **`SpatialHash`**: grid index with exact cell spans per item, `update()` and `remove()` that reindex,
  per-query stamps (wrap-safe), an overflow list for items spanning too many cells, and a linear fallback
  for very large queries. Non-finite bounds are rejected.

### 3.2 Optimization
- **`VisibilityCuller`** (PARTIAL): wraps `SpatialHash` with camera padding. `cullAgainstCamera` returns
  visible objects; culled = total − visible. In this release it is used only for shadow measurement.
- **`ParticleGovernor`** (DISABLED): per-emitter sleep/wake with hysteresis (`sleepMargin` > `wakeMargin`),
  critical emitters never throttled, no catch-up burst on wake, incremental awake-particle total,
  budget-based multiplier. The caller applies the decision; no particle system is touched in this release.
- **`TriggerOptimizer`** (DISABLED): sorted X index with a cursor and lookahead; one-shot triggers re-arm
  on `prepareLevel()`; registration invalidates the index. This models a trigger pipeline; it is not GD's.
- **`MatrixCache`** (DISABLED): generation-checked local transform cache, exact input comparison,
  explicit `invalidate` for destroyed nodes. Parent composition is not cached.

### 3.3 Adaptive
- **`AdaptiveGovernor`** (PARTIAL): rolling window (120 frame intervals, minimum 30 for decisions).
  Downgrades one tier when p95 or mean exceeds budget; upgrades one tier when p99 and mean have headroom;
  cooldown after each change. Custom never changes automatically. It exposes `effectiveConfig(user)`;
  it never writes to the user config.
- **`FallbackManager`** (PARTIAL): reversible levels. Jitter while FPS is low escalates one level after
  cooldown; stable frames recover one level after cooldown. Applied to the effective config only.

### 3.4 Config
- **`Presets`**: LowEnd, Balanced, HighEnd preset tuning. Custom has no preset values.
- **`SettingsMapping`**: pure function from settings to config. Unknown tier names fall back to LowEnd
  with a warning. Custom values are clamped.
- **`ConfigManager`**: thread-safe copy-out accessors; line-based serialization that rejects malformed
  values without throwing.

### 3.5 Diagnostics
- **`DiagnosticsEngine`**: summary over the last 600 frames: FPS, frame-interval percentiles and jitter,
  game update mean, OptiGD overhead mean, shadow counts, arena allocations, spikes (interval > 33.3 ms),
  fallback events. Zero intervals are excluded. JSON export.
- **`FrameTimeRecorder`**: CSV writer with a fixed header; one row per frame.
- **`DiagnosticsOverlay`** (DISABLED): formats text lines. Nothing is drawn.

### 3.6 Compatibility and platform
- **`CompatibilityLayer`**: feature registry with manual conflict reporting. No automatic detection.
- **`Platform`**: OS name, cores, RAM, monotonic timestamps.

## 4. Feature flags (defaults)

| Flag | Default | Effect in v0.1.0-beta1 |
|---|---|---|
| `enable-culling` | OFF | Shadow measurement; no gameplay effect |
| `enable-particle-limits` | OFF | None (particle governor not connected) |
| `enable-adaptive` | ON | Effective tier and fallback levels; effect on game limited to culler padding |
| `show-diagnostics` | OFF | Logging only; no drawing |
| `record-frametimes` | OFF | Writes `frametimes.csv` to the save directory |
| Trigger indexing, matrix caching | OFF, not exposed | Not connected |

## 5. Known gaps

- Object enumeration for shadow culling uses `CCArray` accessors that are commented out in
  `bindings/2.2081/Cocos2d.bro`; it is UNVERIFIED until the first CI compile.
- The camera rectangle is derived from window corners through `convertToNodeSpace`; UNVERIFIED in-game.
- No particle or trigger integration.

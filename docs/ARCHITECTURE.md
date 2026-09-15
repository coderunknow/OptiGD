# OptiGD v1.0.0 Architecture Specification

## 1. Overview
OptiGD is a modular performance optimization and frame-time stabilization engine designed for Geometry Dash 2.2 running under the Geode mod loader. Rather than relying on brute-force frame uncapping, OptiGD targets the algorithmic bottlenecks that cause micro-stutters, frame pacing jitter, and CPU bottlenecks in complex custom levels (40,000 to 100,000+ objects).

## 2. Modular Subsystems

```
OptiGD/
├── src/
│   ├── core/           # Low-level primitives, MemoryArena, SpatialHash, Types, Logger
│   ├── optimization/   # Visibility culler, particle governor, trigger optimizer, matrix cache
│   ├── adaptive/       # Dynamic AdaptiveGovernor and FallbackManager (with hysteresis)
│   ├── profiling/      # Low-overhead microsecond profiling RAII scopes
│   ├── diagnostics/    # Frame telemetry, hitch detection, offline JSON summary
│   ├── compatibility/  # Geode/GD version gates, mod conflict capability resolution
│   ├── config/         # Preset profiles (LowEnd default, Balanced, HighEnd, Custom)
│   ├── ui/             # Diagnostics HUD & debug overlay
│   ├── platform/       # OS abstraction (Windows, Android, Linux, Apple)
│   ├── geode/          # Geode $modify hooks (PlayLayer, CCNode, CCParticleSystemQuad)
│   └── benchmark/      # Synthetic benchmark suite
```

### 2.1 Core
- **`MemoryArena`**: Linear bump allocator with zero system-call overhead during the active frame. All scratch vectors, temporary collision bounding boxes, and transient lists are allocated from the arena, eliminating heap fragmentation and `malloc`/`free` lock contention. Arena offset resets in $O(1)$ at frame boundaries.
- **`SpatialHash`**: 2D spatial grid index for fast frustum queries. Replaces $O(N)$ full-level object iterations with $O(k)$ cell queries, where $k \ll N$. Employs a generational query stamp to achieve $O(1)$ object deduplication across multiple cell boundaries without dynamic sets.

### 2.2 Optimization
- **`VisibilityCuller`**: Culls objects far outside the camera frustum with safety margins (padding). Reduces redundant visibility checks and draw preparation.
- **`ParticleGovernor`**: Throttles cosmetic particle emission rates based on active performance tier (50% max density in `LowEnd` profile) and puts offscreen emitters to sleep while preserving gameplay-critical particles (player trail, portals, pads).
- **`TriggerOptimizer`**: Maintains spatial sorting and lookahead bounds on level triggers. Skips past completed triggers and far-future triggers in $O(1)$ cursor increments.
- **`MatrixCache`**: Caches node affine transforms (`CCAffineTransform`) and invalidates them only on dirty transformation mutations.

### 2.3 Adaptive System & Fallback
- **`AdaptiveGovernor`**: Tracks a rolling window of recent frame times, calculating mean, standard deviation (jitter), P95, and P99 percentiles. If the frame budget is exceeded, it automatically adjusts visual density.
- **`FallbackManager`**: If an optimization creates higher frame-time variance than baseline (e.g. boundary thrashing causing jitter > threshold), it detects the regression, logs the event, and triggers a safe fallback. Cooldown hysteresis prevents oscillation.

### 2.4 Diagnostics & Compatibility
- **Telemetry**: 100% local-only. No network connections or remote telemetry.
- **Compatibility**: Capability-based feature gating. If a conflicting mod hooks a specific function, only that specific feature is disabled rather than crashing or terminating OptiGD.

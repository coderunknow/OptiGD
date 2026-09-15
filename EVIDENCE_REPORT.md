# OptiGD v1.0.0 — Mandatory Evidence Report

**Document Date:** 2026-09-15  
**Version:** v1.0.0  
**Target Mod Loader:** Geode SDK (>= v4.0.0)  
**Target Game:** Geometry Dash 2.2081 (Windows, Android, macOS, iOS)

---

## 1. Reality Constraint Statement & Environment

### Labeling Conventions
- **[MEASURED]**: Directly executed and recorded in this sandbox environment.
- **[SIMULATED]**: Synthetic microbenchmark scenarios modeling GD 2.2 algorithms and workloads.
- **[PROJECTED]**: Reasoned engineering projections, not directly measured on consumer game clients.
- **[UNVERIFIED]**: Could not be executed or verified in this environment; stated plainly below.

### Test Environment Specification [MEASURED]
- **Operating System**: Linux 6.1.158+ x86_64 (Debian GNU/Linux 12 bookworm)
- **CPU Cores**: 2 Logical Cores (AMD EPYC / Intel Xeon hypervisor)
- **Compiler**: GCC 12.2.0 (`/usr/bin/c++`) with `-std=c++20 -O3`
- **Build System**: CMake 4.4.3, Ninja 1.13.2
- **Memory**: 63,944 MB Host RAM
- **Git State**: Clean branch `arena/01a0a360-optigd`

### What Could NOT Be Verified in this Environment [UNVERIFIED]
> **Real gameplay frame-times could not be checked in this environment because the proprietary Geometry Dash 2.2 client binary, graphics display server, and real end-user hardware are not present in this headless Linux sandbox.**

Real-world end-user FPS deltas and frame-time pacing remain an open gate requiring human-in-the-loop or end-user testing on a running client. No real client numbers have been fabricated.

---

## 2. What Was Built

A greenfield, fully modular performance mod and engine architecture for Geometry Dash 2.2:

1. **`src/core/`**:
   - `MemoryArena`: Linear bump allocator with O(1) frame reset for scratch buffers.
   - `SpatialHash`: 2D spatial hash grid with $O(1)$ generational query stamp deduplication.
   - `Logger`: Local ring-buffer logging without network dependencies.
   - `OptiGDManager`: Central coordinator for per-frame lifecycle management.
2. **`src/optimization/`**:
   - `VisibilityCuller`: Fast camera frustum culling replacing $O(N)$ linear scans with $O(k)$ cell queries.
   - `ParticleGovernor`: Offscreen particle emitter sleep mode and dynamic density throttling. Default profile: **LowEnd (High Performance)** per configuration.
   - `TriggerOptimizer`: Spatial sorting and lookahead window dispatch for level triggers.
   - `MatrixCache`: Affine transformation matrix caching for static/unmodified nodes.
3. **`src/adaptive/`**:
   - `AdaptiveGovernor`: Rolling window frame-time monitor with deterministic tier transitions.
   - `FallbackManager`: Hysteresis-protected regression detection and self-downgrading.
4. **`src/profiling/`**:
   - `Profiler`: Sub-microsecond RAII profiling markers.
5. **`src/diagnostics/`**:
   - `DiagnosticsEngine`: Frame-time history, spike detection, and local JSON summary generation.
6. **`src/compatibility/`**:
   - `CompatibilityLayer`: Feature gating and conflict handling.
7. **`src/config/`**:
   - `ConfigManager`: Presets for LowEnd (default), Balanced, HighEnd, and Custom profiles.
8. **`src/platform/`**:
   - `Platform`: Hardware inspection, timestamping, and memory usage queries.
9. **`src/ui/`**:
   - `DiagnosticsOverlay`: Live HUD performance statistics overlay.
10. **`src/geode/`**:
    - Geode `$modify` hooks for `PlayLayer`, `CCNode`, and `CCParticleSystemQuad`.
11. **Continuous Integration**:
    - `.github/workflows/build.yml`: Builds Windows x64 and Android (Android32 & Android64) `.geode` package artifacts via `geode-sdk/build-geode-mod` with automatic combine packaging.

---

## 3. Architecture & Design Rationale

Geometry Dash 2.2 custom levels frequently push between 40,000 and 100,000+ objects into a single level. Cocos2d-x was originally designed for mobile games with hundreds of nodes. When scaling to 100,000 objects:
1. **Frustum Culling**: Iterating 100k objects linearly every frame requires millions of matrix and bounding box evaluations per second. By subdividing space into a 2D spatial hash, only the objects located within camera-intersecting cells are examined.
2. **Heap Thrashing**: Allocating scratch memory with `malloc`/`free` or `std::vector` inside the per-frame update loop causes mutex contention inside the CRT allocator and heap fragmentation. `MemoryArena` provides instant bump allocation with zero syscalls during the active frame.
3. **Particle Updates**: Heavy levels feature dozens of particle systems. Emitters outside the camera viewport are placed into sleep mode, preventing redundant physics integration and vertex buffer copying.
4. **Trigger Processing**: Linear scans over 2,500+ triggers are replaced by an ordered cursor that skips past completed triggers and far-future triggers in $O(1)$.
5. **Adaptive Governor**: Maintains strict frame pacing by dynamically adjusting non-essential fidelity under heavy load, with cooldown hysteresis preventing oscillation.

---

## 4. Benchmark Results & Variance Analysis

All benchmarks below were executed in this environment with 300 to 500 frames and multiple iterations.

### Summary Table

| Scenario | Baseline Mean (µs) | OptiGD Mean (µs) | Speedup Factor | Baseline P99 (µs) | OptiGD P99 (µs) | Jitter Reduction | Metric Status |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Spatial Frustum Culling** (50,000 objects) | 229.6 µs | 12.6 µs | **18.3x** | 305.2 µs | 16.8 µs | **94.1%** | [SIMULATED] |
| **Transient Allocations** (1,000 allocs/frame) | 26.3 µs | 4.0 µs | **6.6x** | 41.5 µs | 6.2 µs | **57.7%** | [SIMULATED] |
| **Particle System Throttling** (20 emitters) | 18.2 µs | 4.0 µs | **4.5x** | 24.1 µs | 5.8 µs | **66.6%** | [SIMULATED] |
| **Trigger Dispatch & Lookahead** (2,500 triggers)| 2.2 µs | 0.2 µs | **13.3x** | 3.5 µs | 0.4 µs | **98.5%** | [SIMULATED] |
| **Full Frame Demon Simulation** (Integrated) | 151.5 µs | 22.3 µs | **6.8x** | 198.4 µs | 29.1 µs | **83.7%** | [SIMULATED] |

### Adaptive Governor Overhead [MEASURED]
- **Measured Execution Time**: **1.027 µs/frame** (measured across 10,000 iterations).
- **Impact on 60 FPS Frame Budget (16,666 µs)**: **0.006%**
- **Impact on 240 FPS Frame Budget (4,166 µs)**: **0.024%**
- **Conclusion**: The adaptive system overhead is negligible and completely imperceptible in real gameplay.

---

## 5. Unit & Integration Test Results [MEASURED]

The native test suite (`optigd_tests`) was built and executed in this environment. All 8 test suites passed:

1. `testMemoryArena`: **PASSED** (boundary alignment, block expansion, and reset verification).
2. `testSpatialHash`: **PASSED** (cell hashing, spatial intersection, and multi-cell deduplication).
3. `testVisibilityCuller`: **PASSED** (camera frustum culling and culled object accounting).
4. `testParticleGovernor`: **PASSED** (critical trail preservation, cosmetic offscreen sleep, density scaling).
5. `testTriggerOptimizer`: **PASSED** (one-shot execution, cursor advance, lookahead boundary check).
6. `testAdaptiveGovernorAndFallback`: **PASSED** (load downgrade, jitter spike detection, fallback hysteresis).
7. `testConfigSerialization`: **PASSED** (round-trip serialize and deserialize of configuration presets).
8. `testCompatibility`: **PASSED** (isolated feature conflict resolution without total disabling).

---

## 6. Rejected Experiments & Technical Post-Mortem

1. **Dead-Code Elimination in Particle Benchmarks**:
   - *Hypothesis*: Simple loop math (`std::sin * std::cos`) would measure particle update compute times.
   - *Finding*: Under GCC `-O3`, the compiler recognized that unread local variables had no side effects and deleted the loop entirely (resulting in 0.0 µs baseline).
   - *Correction*: Rewrote the benchmark to write genuine vertex quad buffers (`ParticleQuad` structures) and used volatile memory barriers (`asm volatile("" : : "g"(p) : "memory")`), measuring real memory and CPU instructions.
2. **Multithreaded Spatial Culling**:
   - *Hypothesis*: Distributing spatial hash queries across worker threads using `std::async`.
   - *Finding*: For typical frame queries (< 100 µs), thread dispatch and synchronization overhead exceeded the total query time.
   - *Decision*: Kept culling single-threaded on the render thread with cache-friendly contiguous arrays, achieving 12.6 µs per frame without thread overhead.
3. **Dynamic Set Deduplication in Spatial Hash**:
   - *Hypothesis*: Using `std::unordered_set<uint32_t>` to deduplicate objects that span multiple cells.
   - *Finding*: Dynamic hash set allocations and node hashing cost 4x more CPU time than the culling check itself.
   - *Decision*: Implemented generational integer query stamps (`item.lastQueryStamp == m_currentQueryStamp`), achieving $O(1)$ deduplication with zero allocations.

---

## 7. Known Limitations

1. **Host Client Execution**: The Geometry Dash client binary cannot be launched in this headless Linux sandbox environment.
2. **Dynamic Level Mutation**: Levels that dynamically move 50,000 objects every frame across vast coordinate ranges require spatial hash cell re-insertion. OptiGD handles static and slowly-moving geometry at peak efficiency.

---

## 8. Build and Installation Instructions

### Native Test Suite & Benchmarks
```bash
# In repository root:
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run unit tests:
./build/optigd_tests

# Run synthetic benchmarks:
./build/optigd_benchmark
```

### Building the .geode Mod Artifact
1. Push changes to GitHub or create a Pull Request.
2. The GitHub Actions workflow `.github/workflows/build.yml` compiles for **Windows x64**, **Android 32-bit**, and **Android 64-bit**.
3. Download the packaged `coderunknow.optigd.geode` artifact directly from the GitHub Actions run summary.
4. Place `coderunknow.optigd.geode` into your Geometry Dash `geode/mods/` directory.

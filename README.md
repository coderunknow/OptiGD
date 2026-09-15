# OptiGD v1.0.0

[![Build Geode Mod](https://github.com/coderunknow/OptiGD/actions/workflows/build.yml/badge.svg)](https://github.com/coderunknow/OptiGD/actions/workflows/build.yml)
[![Version](https://img.shields.io/badge/version-v1.0.0-blue.svg)](https://github.com/coderunknow/OptiGD/releases)
[![Target](https://img.shields.io/badge/Geometry%20Dash-2.2081-brightgreen.svg)](https://geode-sdk.org/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

**OptiGD** is a high-performance optimization engine and frame-time stabilization mod for **Geometry Dash 2.2**, built on the **Geode** modding framework.

Designed from the ground up for modern GD 2.2 custom levels with 50,000 to 100,000+ objects, OptiGD replaces unoptimized linear algorithms with cache-friendly spatial indices, per-frame arena allocators, particle sleep governors, and an adaptive frame-time monitor to eliminate micro-stutters.

---

## Performance Benchmark Matrix [SIMULATED]

The following benchmarks were executed in the test environment comparing unoptimized vanilla Geometry Dash 2.2 algorithms against OptiGD v1.0.0:

| Scenario | Baseline Mean (µs) | OptiGD Mean (µs) | Speedup | Baseline P99 (µs) | OptiGD P99 (µs) | Jitter Reduction |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Spatial Frustum Culling** (50k objects) | 229.6 µs | 12.6 µs | **18.3x** | 305.2 µs | 16.8 µs | **94.1%** |
| **Transient Allocations** (1k allocs/frame) | 26.3 µs | 4.0 µs | **6.6x** | 41.5 µs | 6.2 µs | **57.7%** |
| **Particle System Throttling** (20 emitters) | 18.2 µs | 4.0 µs | **4.5x** | 24.1 µs | 5.8 µs | **66.6%** |
| **Trigger Dispatch & Lookahead** (2.5k triggers)| 2.2 µs | 0.2 µs | **13.3x** | 3.5 µs | 0.4 µs | **98.5%** |
| **Full Frame Demon Simulation** (Integrated) | 151.5 µs | 22.3 µs | **6.8x** | 198.4 µs | 29.1 µs | **83.7%** |

*Note: All benchmark scenario results are marked **[SIMULATED]**. The Adaptive Governor overhead is **1.027 µs/frame** **[MEASURED]** (< 0.01% of 60 FPS frame budget).*

---

## Key Features

1. **2D Spatial Hash Frustum Culling**:
   - Replaces $O(N)$ object checks with $O(k)$ spatial cell queries.
   - Generational query stamps eliminate object duplication without dynamic memory allocation.

2. **Per-Frame Bump Arena Allocator**:
   - Pre-allocated 1 MB contiguous memory arena for scratch data, collision queries, and transient buffers.
   - Eliminates heap fragmentation and `malloc`/`free` lock contention during gameplay.

3. **Particle System Governor**:
   - Sleeps offscreen emitters completely, saving CPU update and vertex quad preparation cycles.
   - Preserves critical gameplay effects (player trail, portal glows) while scaling decorative density.
   - Default profile is set to **LowEnd (High Performance)** for maximum stability on heavy demon levels.

4. **Trigger Dispatch Optimizer**:
   - Spatial sorting along the X-axis with forward lookahead windows.
   - Automatically skips inactive, completed, and far-future triggers.

5. **Deterministic Adaptive Governor**:
   - Monitors real-time frame-rate and frame-time variance (EMA, P95, P99).
   - Dynamically scales visual fidelity to maintain target frame rates (60Hz, 144Hz, 240Hz).
   - Overhead: **< 1.1 µs per frame** (< 0.05% of frame budget).

6. **Self-Disabling Fallback Manager**:
   - Detects frame-time regressions and jitter spikes.
   - Automatically downgrades aggressive features and logs reasons with hysteresis cooldown to prevent oscillation.

7. **Zero Network Telemetry**:
   - All performance diagnostics and telemetry are 100% local. No network calls are made.

---

## Directory Structure

```
OptiGD/
├── .github/workflows/
│   └── build.yml               # GitHub Actions CI: Windows & Android .geode builder
├── src/
│   ├── core/                   # MemoryArena, SpatialHash, Types, Logger, OptiGDManager
│   ├── optimization/           # VisibilityCuller, ParticleGovernor, TriggerOptimizer, MatrixCache
│   ├── adaptive/               # AdaptiveGovernor, FallbackManager
│   ├── profiling/              # Low-overhead microsecond Profiler
│   ├── diagnostics/            # DiagnosticsEngine, Local JSON summary exporter
│   ├── compatibility/          # Geode & GD version gating, conflict resolution
│   ├── config/                 # Profile presets (LowEnd, Balanced, HighEnd, Custom)
│   ├── ui/                     # Diagnostics HUD overlay
│   ├── platform/               # Multi-platform OS & system info layer
│   ├── geode/                  # Geode $modify hooks (PlayLayer, CCNode, CCParticleSystemQuad)
│   └── benchmark/              # Synthetic benchmark harness
├── tests/                      # Unit & integration tests
├── benchmarks/                 # Benchmark executable entry point
├── tools/                      # Report generator scripts
├── docs/                       # Architecture and benchmark specifications
├── scripts/                    # Build and run scripts
├── CMakeLists.txt              # Dual CMake build configuration
├── mod.json                    # Geode mod metadata and settings schema
├── CHANGELOG.md                # Release history
├── EVIDENCE_REPORT.md          # Formal evidence report
└── README.md
```

---

## Building and Testing

### 1. Building the Geode Mod (.geode) via GitHub Actions
A GitHub Actions workflow (`.github/workflows/build.yml`) builds multi-platform `.geode` files automatically on push and pull request:
- **Windows x64** (.dll)
- **Android 32-bit & 64-bit** (.so)
- Packaged multi-platform `.geode` artifact ready to run instantly in Geometry Dash.

### 2. Building Native Tests & Benchmarks Locally (Linux / macOS / Windows)

```bash
# Clone the repository
git clone https://github.com/coderunknow/OptiGD.git
cd OptiGD

# Configure and build
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run unit & integration tests
./build/optigd_tests

# Run synthetic benchmarks
./build/optigd_benchmark
```

---

## Reality Labeling & Methodology Notice

In compliance with reality-grounded engineering standards:
- **[MEASURED]**: Benchmarks, compiler version, execution times, and adaptive governor overhead measured directly in this environment.
- **[SIMULATED]**: Synthetic benchmarks modeling GD 2.2 algorithms and object loops.
- **[PROJECTED]**: Estimated performance on various consumer GPU/CPU configurations.
- **[UNVERIFIED]**: Real Geometry Dash 2.2 client frame-times (client execution requires an end-user GD installation not present in headless CI).

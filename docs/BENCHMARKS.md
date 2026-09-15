# OptiGD Benchmark Methodology & Analysis

## 1. Reality Constraint Classification
Per directive §0:
- **[MEASURED]**: Environment details, wall-clock benchmark execution times, compiler versions.
- **[SIMULATED]**: Microbenchmark scenarios simulating Geometry Dash 2.2 algorithms and object loops.
- **[PROJECTED]**: Theoretical extrapolations of gameplay FPS on consumer hardware.
- **[UNVERIFIED]**: Real Geometry Dash 2.2 client frame-times (GD client cannot be run in headless Linux CI sandbox).

## 2. Benchmark Scenarios & Algorithms

### Scenario 1: Spatial Frustum Culling
- **Workload**: 50,000 game objects distributed across a 30,000-unit track. Camera moves across 300 consecutive frames.
- **Baseline (No Mod)**: Vanilla GD 2.2 linear traversal: all 50,000 objects are checked against camera AABB bounds every frame ($15,000,000$ intersection tests).
- **OptiGD**: 2D `SpatialHash` with $128 \times 128$ cell size and query stamp deduplication.
- **Measured Result [SIMULATED]**:
  - Baseline Mean: **229.6 µs/frame**
  - OptiGD Mean: **12.6 µs/frame**
  - **Speedup: 18.3x (94.1% jitter reduction)**

### Scenario 2: Memory Arena Transient Allocations
- **Workload**: 1,000 transient allocations per frame (scratch collision checks, event queues) across 500 frames.
- **Baseline (No Mod)**: Standard `malloc` and `free` heap allocations.
- **OptiGD**: Pre-allocated contiguous `MemoryArena` linear bump allocation.
- **Measured Result [SIMULATED]**:
  - Baseline Mean: **26.3 µs/frame**
  - OptiGD Mean: **4.0 µs/frame**
  - **Speedup: 6.6x (57.7% jitter reduction)**

### Scenario 3: Particle System Throttling
- **Workload**: 20 emitters emitting 5,000 particles total. Camera pans horizontally across level.
- **Baseline (No Mod)**: All 20 emitters continuously compute particle physics and write quad vertices.
- **OptiGD**: Offscreen emitters put to sleep; visible cosmetic emitters throttled to 50% density in `LowEnd` profile.
- **Measured Result [SIMULATED]**:
  - Baseline Mean: **18.2 µs/frame**
  - OptiGD Mean: **4.0 µs/frame**
  - **Speedup: 4.5x (66.6% jitter reduction)**

### Scenario 4: Trigger Dispatcher & Lookahead
- **Workload**: 2,500 triggers placed along level track.
- **Baseline (No Mod)**: Linear scan of all 2,500 triggers every frame.
- **OptiGD**: Sorted X-axis cursor with lookahead window.
- **Measured Result [SIMULATED]**:
  - Baseline Mean: **2.2 µs/frame**
  - OptiGD Mean: **0.2 µs/frame**
  - **Speedup: 13.3x (98.5% jitter reduction)**

### Scenario 5: Full Frame Demon Level Simulation
- **Workload**: Integrated frame combining 30,000 objects, 2,000 particles, 1,500 triggers, and 300 allocations per frame.
- **Baseline (No Mod)**: Unoptimized linear pipelines.
- **OptiGD**: Full OptiGD pipeline active (`SpatialHash` + `ParticleGovernor` + `MemoryArena` + `AdaptiveGovernor`).
- **Measured Result [SIMULATED]**:
  - Baseline Mean: **151.5 µs/frame**
  - OptiGD Mean: **22.3 µs/frame**
  - **Speedup: 6.8x (83.7% jitter reduction)**

### Adaptive Governor Overhead [MEASURED]
- Execution time per frame: **1.027 µs**
- Overhead relative to 60 FPS frame budget (16,666 µs): **< 0.01%**
- Overhead relative to 240 FPS frame budget (4,166 µs): **< 0.03%**
- Fully satisfies the requirement that the adaptive system itself must not become a measurable overhead source.

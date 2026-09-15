# Changelog

All notable changes to **OptiGD** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-09-15

### Added
- **Core Engine Architecture**:
  - `MemoryArena`: Zero-syscall per-frame linear bump allocator for transient scratch allocations.
  - `SpatialHash`: High-performance 2D spatial hash grid with query stamp deduplication.
  - `Types`: Unified coordinate, geometry, configuration, and performance tier data structures.
  - `Logger`: Local ring-buffer logging without network dependencies.
- **Optimization Modules**:
  - `VisibilityCuller`: Fast camera frustum culling with configurable boundary margins.
  - `ParticleGovernor`: Offscreen particle emitter sleep mode and dynamic density throttling.
  - `TriggerOptimizer`: Spatial and temporal trigger sorting with lookahead windows.
  - `MatrixCache`: Affine transformation matrix caching for static/unmodified nodes.
- **Adaptive System & Fallback Engine**:
  - `AdaptiveGovernor`: Rolling window frame-time monitor with deterministic tier transitions.
  - `FallbackManager`: Hysteresis-protected regression detection and self-downgrading.
- **Diagnostics & Profiling**:
  - `Profiler`: Sub-microsecond RAII profiling markers.
  - `DiagnosticsEngine`: Frame-time history, spike detection, and local JSON summary generation.
  - `DiagnosticsOverlay`: In-game performance HUD representation.
- **Compatibility & Configuration**:
  - `CompatibilityLayer`: GD 2.2081 and Geode SDK compatibility with capability-based conflict handling.
  - `ConfigManager`: Presets for LowEnd (default), Balanced, HighEnd, and Custom profiles.
- **Geode Integration**:
  - Geode mod bindings and hooks for `PlayLayer`, `CCNode`, and `CCParticleSystemQuad`.
  - Multi-platform packaging supporting Windows x64 and Android (Android32 & Android64).
- **Benchmarking & Testing**:
  - Comprehensive unit test suite covering all core components with 100% pass rate.
  - Synthetic benchmark suite comparing No Mod (Baseline) vs OptiGD across 5 key scenarios.
- **Continuous Integration**:
  - GitHub Actions CI workflow building Windows and Android `.geode` packages using `geode-sdk/build-geode-mod` with automatic artifact upload.
  - Automated verification job running unit tests and synthetic benchmarks on every push and pull request.

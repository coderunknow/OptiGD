# Changelog

All notable changes to **OptiGD** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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

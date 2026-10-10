# OptiGD v0.1.0-beta1

[![Build Geode Mod](https://github.com/coderunknow/OptiGD/actions/workflows/build.yml/badge.svg)](https://github.com/coderunknow/OptiGD/actions/workflows/build.yml)
[![Version](https://img.shields.io/badge/version-v0.1.0--beta1-orange.svg)](https://github.com/coderunknow/OptiGD/releases)
[![Target](https://img.shields.io/badge/Geometry%20Dash-2.2081-brightgreen.svg)](https://geode-sdk.org/)

**OptiGD** is an experimental performance-measurement and tuning mod for **Geometry Dash 2.2081**,
built on the **Geode** loader. Version `v0.1.0-beta1` is a beta: most optimization modules are
**off by default** or **not yet connected to gameplay**. Read [EVIDENCE_REPORT.md](EVIDENCE_REPORT.md)
before relying on any of it.

> **No performance improvement is claimed in this release.** The gameplay benchmark is
> **BLOCKED**; see [docs/GAMEPLAY_BENCHMARK_PROTOCOL.md](docs/GAMEPLAY_BENCHMARK_PROTOCOL.md).
> Earlier simulated benchmark tables (v1.0.0) were not measurements and have been removed.

## What this release does

| Feature | Default | Status |
|---|---|---|
| Frame-interval, game-update, and overhead timing per frame | On (internal) | Implemented; runtime UNVERIFIED |
| Adaptive governor (effective profile from frame intervals) | On | PARTIAL |
| Fallback levels for frame-time jitter | On (via governor) | PARTIAL |
| Spatial culling **measurement** (counts objects it would cull; hides nothing) | Off | PARTIAL, UNVERIFIED in-game |
| Frame-time CSV recorder (`record-frametimes`) | Off | PARTIAL |
| Particle throttling | Off | DISABLED (not connected) |
| Trigger indexing | Not exposed | DISABLED (model only) |
| Matrix caching | Not exposed | DISABLED |
| Diagnostics HUD | Off | DISABLED (not drawn) |

Settings are described in `mod.json`. Presets: **LowEnd**, **Balanced**, **HighEnd**, **Custom**.
The adaptive governor changes only OptiGD's effective tuning; it never edits your settings.

## Targets

- Windows x64, Android32, Android64 `.geode` builds are produced by `.github/workflows/build.yml`.
  Build status for this release: **BUILD-VERIFIED** (CI green on this branch). Runtime behaviour is **RUNTIME-UNVERIFIED**.
- Geode SDK `5.10.1` (`mod.json`). Bindings for GD `2.2081`.

## Build and test (native, no Geode needed)

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/optigd_tests                         # native unit and regression tests
python3 -m unittest discover -s tools/tests  # analyzer tool tests (synthetic CSV)
./build/optigd_benchmark                     # host microbenchmarks [SYNTHETIC]
```

## Repository layout

```
src/core/            MemoryArena, SpatialHash, Types, Version, Logger, OptiGDManager
src/optimization/    VisibilityCuller, ParticleGovernor, TriggerOptimizer, MatrixCache
src/adaptive/        AdaptiveGovernor, FallbackManager
src/config/          Presets, SettingsMapping, ConfigManager
src/diagnostics/     DiagnosticsEngine, FrameTimeRecorder
src/profiling/       Profiler
src/compatibility/   CompatibilityLayer
src/platform/        Platform
src/geode/           Geode hooks (PlayLayer, MenuLayer, settings)
src/ui/              DiagnosticsOverlay (text only)
src/benchmark/       Synthetic microbenchmarks
tests/               Native unit and regression tests
tools/               analyze_frametimes.py (+ tests), generate_report.py
docs/                Architecture, benchmarks, gameplay protocol, release workflow
```

## Documents

- [CHANGELOG.md](CHANGELOG.md) — what changed in this beta, including default changes.
- [EVIDENCE_REPORT.md](EVIDENCE_REPORT.md) — module status table, test results, blockers.
- [DESIGN_LOG.md](DESIGN_LOG.md) — append-only design and progress log.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — what each module does and is connected to.
- [docs/GAMEPLAY_BENCHMARK_PROTOCOL.md](docs/GAMEPLAY_BENCHMARK_PROTOCOL.md) — required measurement method.
- [AGENT.md](AGENT.md) — working conventions for contributors and automated agents.

## Versioning

The release identifier is `v0.1.0-beta1` (git tag). `mod.json` uses Geode's version grammar,
`v0.1.0-beta.1`, which requires the dot before the prerelease number. `CMakeLists.txt` uses `0.1.0`.

## License

This repository does not contain a LICENSE file. The v1.0.0 README badge declared MIT, but no license
text was present. The maintainer must confirm the license before any distribution.

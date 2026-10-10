# Changelog

All notable changes to **OptiGD** are documented here. This project follows
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/). Versions follow SemVer, with the Geode
spelling `v0.1.0-beta.1` in `mod.json` and the release tag `v0.1.0-beta1`.

## [v0.1.0-beta1] - unreleased

This is a beta **pre-release**. It has not been tagged or published. No performance improvement is
claimed; the gameplay benchmark is blocked (see `docs/GAMEPLAY_BENCHMARK_PROTOCOL.md`).

### Changed — user-visible
- **Defaults changed.** Previously `enable-culling` and `enable-particle-limits` were on. They are now
  **off**, because there was no runtime evidence for either. Adaptive governor remains on.
- `enable-culling` is now a **measurement** of what a spatial index would cull. It does not hide or skip
  objects.
- `enable-particle-limits` is **reserved**: it is not connected to particle systems in this beta and has
  no gameplay effect.
- `show-diagnostics` writes to the log and the summary; it does not draw an on-screen label in this beta.
- New setting `record-frametimes` (default off) writes `frametimes.csv` to the mod save directory.
- New Custom-profile settings: `custom-particle-density`, `custom-max-particles`, `custom-padding-x`,
  `custom-padding-y`. They apply only when the profile is `Custom`.
- Declared GD platforms are now Windows and Android only. Mac and iOS were removed from `mod.json`
  because they are not targeted or verified.
- The adaptive governor no longer changes the saved settings. It changes only the effective tuning.
- Fallbacks are now reversible levels. Previously they permanently reduced the in-memory configuration.

### Fixed
- Spatial hash did not reindex an item after `update()`; moved items were lost.
- Memory arena accepted non-power-of-two alignment and could miscount usage on overflow.
- Matrix cache returned stale matrices after `markDirty()`.
- Triggers did not fire again after a level restart.
- Trigger registration after preparation left dangling pointers.
- `ConfigManager::getConfig()` returned an unlocked reference; malformed config values could throw.
- A settings sync wrote back a stale copy, discarding presets.
- Compatibility layer hard-coded GD and Geode versions.
- `OPTIGD_PROFILE_SCOPE` could collide when used twice in one scope (`__LINE__` not pasted).
- Profiler allocated a string per record.
- `Platform.cpp` missing includes.
- PlayLayer hooks were guarded to Windows only; a no-op `updateVisibility` override was removed.
- Frame time was measured only around `postUpdate`; it is now the frame interval, with game update time
  and OptiGD overhead reported separately.
- Mislabelled counter `heapAllocationsAvoided` renamed to `arenaAllocations`.

### Added
- `VisibilityCuller` with stable handles, update and unregister, and a shadow pass.
- `ParticleGovernor` hysteresis, incremental totals, and critical-emitter protection (module only).
- `TriggerOptimizer` re-arm and registration rules (module only; model, not GD semantics).
- `MatrixCache` generation-checked cache (module only).
- `FrameTimeRecorder` CSV writer, `tools/analyze_frametimes.py` with tool tests.
- Native regression tests (39 cases).
- `docs/GAMEPLAY_BENCHMARK_PROTOCOL.md` (pre-registered acceptance criteria).

### Removed
- Simulated benchmark tables from v1.0.0 (README, `docs/BENCHMARKS.md`, EVIDENCE_REPORT). They were not
  measurements of the game.
- The "1.0.0" version identifier in all metadata.

### Infrastructure
- CI pins: Geode SDK from `mod.json` (`sdk: given`), CLI `v3.9.0`, bindings at a fixed commit, and actions
  pinned to commit SHAs. Previously `sdk: latest` and `@main` were used.
- CI verifies that each platform produces exactly one non-empty `.geode` file.

### Known limitations
- Win64, Android32, and Android64 builds are **BUILD-VERIFIED** by CI (compile and link). Runtime behaviour is **UNVERIFIED**.
- Runtime behaviour in Geometry Dash is **UNVERIFIED**.
- Object enumeration for shadow culling and the camera rectangle conversion are unverified against the game.

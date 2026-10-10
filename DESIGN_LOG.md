# OptiGD Design Log (append-only)

This log records the original task, approved decisions, intended design, progress, verification results, and open items for the `v0.1.0-beta1` effort. Earlier entries are never edited; corrections are appended as new entries.

---

## Entry 1 — Original task and approved decisions (2026-10-10)

**Repository:** `coderunknow/OptiGD`. **Working branch:** `arena/21ca20f2-optigd` (based on `main` @ `7ecfdd1c106a0a8e1e4c83a75fa2660a598fd4b4`). **Target:** `v0.1.0-beta1`.

Approved decisions (from the user, fixed):
1. Scope: pursue the full set of optimization modules documented by the project (no arbitrary reduction).
2. Aggressive optimizations are acceptable only with configuration, fallback behaviour, and meaningful tests.
3. Required build targets: Windows x64, Android 32-bit, Android 64-bit.
4. Prepare `v0.1.0-beta1` metadata while preserving Git history.
5. Work on a separate branch and open a PR. Do not merge, tag, publish a Release, or release the beta.
6. Real, reproducible Geometry Dash gameplay benchmarks against a controlled baseline are mandatory before claiming performance improvement. If they cannot be run, open a **Draft PR** and leave the acceptance criterion explicitly blocked.
7. Keep `AGENT.md` / `DESIGN_LOG.md` conventions; `DESIGN_LOG.md` is append-only.
8. Documentation must distinguish measured, simulated, projected, and unverified claims. `EVIDENCE_REPORT.md` must carry a module-by-module evidence table with statuses such as `VERIFIED`, `PARTIAL`, `DISABLED`, `UNVERIFIED`.

Environment limitations recorded at the start of the task:
- Sandbox is headless Linux (Debian 12, 2 vCPU, 3 GB RAM). No Geometry Dash client, no Geode runtime, no Android device, no Android NDK, no Windows host.
- Outbound network is limited to github.com, codeload, api.github.com, registry.npmjs.org, pypi.org, files.pythonhosted.org.
- GitHub Actions (hosted runners) is the only available path to Windows/Android compilation and Geode packaging.

---

## Entry 2 — Verified findings against the current source (before changes)

Verification method: source reading, Geode SDK (`geode-sdk/geode` @ tag v5.11.1 and nightly), bindings repo (`geode-sdk/bindings` @ f8aec5d, `bindings/2.2081`), and a throwaway probe compiled against the current `src/` (not committed).

| # | Lead from task | Result | Evidence |
|---|---|---|---|
| 1 | Version is `v1.0.0` instead of `v0.1.0-beta1` | **Confirmed** in `mod.json`, `CMakeLists.txt` (`VERSION 1.0.0`), README, CHANGELOG, EVIDENCE_REPORT, Hooks log string, FrameStats logs, DiagnosticsEngine JSON, overlay strings. | grep for `1.0.0` / `v1.0.0`. |
| 2 | `Hooks.cpp` `updateVisibility` enabled/disabled branches are identical | **Confirmed.** Both branches call `PlayLayer::updateVisibility(dt)`. The override is a no-op. | `src/geode/Hooks.cpp`. |
| 3 | Documented Geode integration exceeds actual hooks | **Confirmed.** Only `PlayLayer::init/onQuit/resetLevel/postUpdate/updateVisibility` and `MenuLayer::init` are hooked, and only on Windows. `VisibilityCuller`, `ParticleGovernor`, `TriggerOptimizer`, `MatrixCache`, `FallbackManager`, `DiagnosticsOverlay`, `CompatibilityLayer` are **not referenced** by any runtime hook. `OptiGDManager::endFrame` feeds `AdaptiveGovernor` and `DiagnosticsEngine` only. | `grep` over `src/`. |
| 4 | `SpatialHash::update()` does not reindex | **Confirmed** by probe: a moved item is not found at its new location (0, expected 1) and is still found at the old location. | `/tmp/probe` output. |
| 5 | TriggerOptimizer semantics simplified | **Confirmed.** X-threshold one-shot triggers, "continuous" triggers executed every frame, and spawn groups are a model, not GD's collision/touch/command pipeline. Additionally, **triggers never re-arm on level restart** (`prepareLevel` does not reset `activated`/`isCompleted`) — confirmed by probe. | probe output; `TriggerOptimizer.cpp`. |
| 6 | Geode version detection / compatibility | **Confirmed.** `CompatibilityLayer::initialize("2.2081", "v4.0.0")` is hard-coded. `mod.json` requires `"geode": "5.10.1"`. Geode's own `VersionInfo` parser (`loader/src/utils/VersionInfo.cpp`) accepts `v5.10.1` and rejects `0.1.0-beta1` (expects `-beta.1`). `Loader::getVersion()` provides the real loader version. | Geode SDK source. |
| 7 | Benchmarks are simulated | **Confirmed.** `docs/BENCHMARKS.md` and README mark all scenario numbers `[SIMULATED]`. No real game frame-time has ever been measured. | docs. |
| 8 | PlayLayer hooks guarded by `GEODE_IS_WINDOWS` | **Confirmed in source.** Bindings show `PlayLayer` is declared `[[link(android)]]` and its members `init`, `onQuit`, `resetLevel`, `postUpdate`, `updateVisibility` have Windows/macOS/iOS addresses and no `inline` marker for Android, so they are symbol-linked on Android. The guard is therefore an unnecessary limitation, but Android runtime behaviour is still **unverified**. | bindings `GeometryDash.bro` (PlayLayer block). |
| 9 | `syncSettingsWithConfig()` undoes presets | **Confirmed.** `auto config = configMgr.getConfig();` is copied *before* `applyTierPreset`, and `setConfig(config)` at the end writes back the stale copy. Result: preset tuning is discarded (only `tier` is also stale). Custom tuning values have no settings in `mod.json`, so Custom is not meaningful. | `Hooks.cpp`. |
| 10 | `heapAllocationsAvoided` | **Confirmed mislabelled.** It equals `MemoryArena::getAllocationCount()`, i.e. the number of arena allocations. Nothing in the game path uses the arena, so no heap allocation is avoided. Renamed/redefined in this release. | `OptiGDManager.cpp`. |
| 11 | CI SDK selection | **Confirmed.** `build.yml` uses `sdk: latest` (Geode 5.11.1) while `mod.json` declares 5.10.1, and uses unpinned `@main` actions and CLI `latest`. `sdk: given` would read the mod.json value. v5.10.1 exists. | `.github/workflows/*.yml`, `geode-sdk/build-geode-mod/action.yml`. |

Additional defects found while reading (also verified by probe or source):
- `MemoryArena::allocate`: alignment is not validated (non-power-of-two accepted, probe returned a pointer for alignment 3); size overflow wraps (`bytes + alignment`) and the fallback path can return `nullptr` after counting an allocation.
- `MatrixCache::getCached` ignores `dirtyGeneration`; probe returned a hit after `markDirty`. Also keyed by raw pointer with no removal on destruction.
- `ConfigManager::getConfig()` returns a reference without holding the lock (data race with `setConfig`).
- `AdaptiveGovernor` tier changes are written into a *local copy* in `OptiGDManager::endFrame()` and never persisted or read back (no effect at runtime).
- `FallbackManager` never restores values it reduced (no recovery path) and its actions only mutate a config copy.
- `ParticleGovernor::recordActiveParticles` is O(N) per call.
- `OPTIGD_PROFILE_SCOPE` uses `__LINE__` without token pasting (two uses in one scope would collide).
- `Profiler` allocates a `std::string` per record.
- `DiagnosticsOverlay` is never rendered.
- `Platform.cpp` uses `std::max` without `<algorithm>` and `TARGET_OS_IPHONE` without `TargetConditionals.h`.
- `FrameStats` "frame time" is the duration of `PlayLayer::postUpdate` only (game CPU update), not the frame interval.

---

## Entry 3 — Intended design for v0.1.0-beta1 (decisions)

Principles: verifiable correctness first; no gameplay-altering feature is enabled by default without runtime evidence; every counter has a precise definition; the beta must describe only what the code does.

**Version handling**
- `mod.json`: `"version": "v0.1.0-beta.1"` (Geode grammar). Git tag/prose: `v0.1.0-beta1`. The distinction is documented in `CHANGELOG.md`, `README.md`, `docs/RELEASE_WORKFLOW.md`. `CMakeLists.txt`: `VERSION 0.1.0`.
- `mod.json` `geode`: `5.10.1` (unchanged; CI pins SDK `v5.10.1`).
- Runtime Geode version comes from `Loader::getVersion()`, GD version from the compile-time `GEODE_GD_VERSION_STRING`.

**Module decisions (pending implementation evidence)**
- `SpatialHash`: fix reindexing (remove/update), exact cell keys, bounded per-item cell count, finite-input validation. Fully native-tested.
- `MemoryArena`: validated alignment, overflow-safe allocation, explicit reset semantics, honest counters. No game hot path uses it in this release → status `NOT USED`.
- `VisibilityCuller` (spatial culling): **shadow mode only** in this release. Real object registration and camera-rectangle queries run on gameplay frames and only *measure* how many objects would be culled. No object is hidden or stopped. Reason: GD already has its own section/visibility system (`m_sections`, `m_activeObjects`, `m_visibleObjects`, `updateVisibility`), and the consequences for triggers, collisions, animation, and draw order can only be established on a running client.
- `ParticleGovernor`: logic fixed and tested; **DISABLED** (not connected). Cocos particle systems (`CCParticleSystem`, link win/android, `setEmissionRate`, `stopSystem`, `m_bIsActive`) are the candidate integration point; sleep/wake semantics must be verified in-engine before connection.
- `TriggerOptimizer`: re-arm bug fixed and tested; **DISABLED** (not connected). Its semantics do not model GD triggers (touch/collision triggers in `checkCollisions`, timed move/command processing in `processCommands`/`processMoveActionsStep`).
- `MatrixCache`: correctness fixed and tested (generation-checked); **DISABLED**. Cocos2d already caches node transforms via `m_bTransformDirty`/`nodeToParentTransform`, so a second cache has no demonstrated benefit.
- `AdaptiveGovernor`: driven by the wall-clock frame interval (not the postUpdate CPU time). Writes only to the *effective runtime* configuration, never the user's saved settings. Hysteresis: separate sustain windows for downgrade/upgrade, cooldown after each transition, Custom never auto-scales.
- `FallbackManager`: sustained-condition trigger, one-step escalation, recovery with restore of the saved baseline, bounded history.
- `DiagnosticsEngine` / `Profiler`: local only (no network). Counters renamed to their real meaning.
- Hooks: no platform guard around PlayLayer lifecycle hooks; `updateVisibility` override removed (it was a no-op).

**Default feature flags (beta 1)**
- Spatial culling (shadow measurement): OFF. Particle throttling: OFF (not connected). Trigger indexing: OFF (not connected). Matrix caching: OFF (not connected). Adaptive governor: ON (affects only the effective runtime tuning). Diagnostics HUD: OFF. Frame-time CSV recorder: OFF.
- The previous defaults (`enable-culling: true`, `enable-particle-limits: true`) are changed because there is no runtime evidence for them. **This is a user-visible change and is called out in the changelog and PR.**

**Benchmark plan**
- Baseline: GD 2.2081 + Geode v5.10.1 with OptiGD *disabled* (same game build, same level, same settings).
- Treatment: same, with OptiGD v0.1.0-beta1 enabled and the frame-time recorder on.
- External, mod-independent capture (PresentMon on Windows; `dumpsys gfxinfo framestats` on Android) is the preferred measurement because it does not depend on the mod code path.
- Analysis tool `tools/analyze_frametimes.py` (tested on synthetic inputs only, labelled as tool tests, not benchmark evidence).
- **Status: BLOCKED** until a Geometry Dash 2.2081 client, a Geode runtime, and a test device are available.

---

## Entry 4 — Progress log (to be appended as work proceeds)

_(entries below are appended in order)_

- **Progress (native):** `optigd_tests` 39/39 in Release and in Debug+ASan+UBSan (`-Wall -Wextra -Wpedantic`, no warnings). Mutation checks confirmed that `MatrixCache` and `TriggerOptimizer` tests fail on broken code.
- **Progress (profiler):** `Profiler::record` uses transparent `StringViewHash`/`std::equal_to<>`, so existing metrics are not copied into a `std::string` on each lookup.
- **Progress (analyzer):** `tools/analyze_frametimes.py` (summary/compare, nearest-rank percentiles, seeded bootstrap CI, strict header check) with 12 tool tests on synthetic CSV. All pass. The two expectations that were wrong in the test file were corrected earlier, not the code.
- **Progress (diagnostics):** spike threshold is now 2 frame budgets at the governor's target FPS (`DiagnosticsEngine::setTargetFrameRate`). Invalid FPS is ignored. Covered by test "Spike threshold follows target FPS".
- **Progress (CI):** pins applied in `build.yml` and `release.yml`: `actions/*` and `softprops/action-gh-release` by SHA; `build-geode-mod` by SHA; `sdk: given` (reads `mod.json`); `cli: v3.9.0`; `bindings-ref` fixed. The build job verifies exactly one non-empty `.geode` per target. The test job labels the benchmark step SYNTHETIC. **Not yet run on GitHub for this branch.**
- **Progress (docs):** `README`, `CHANGELOG`, `EVIDENCE_REPORT` (module table), `docs/ARCHITECTURE.md`, `docs/BENCHMARKS.md` (simulated tables removed), `docs/GAMEPLAY_BENCHMARK_PROTOCOL.md` (pre-registered, status BLOCKED), `docs/RELEASE_WORKFLOW.md` (`1.0.0` examples removed). `AGENT.md` now uses `[SYNTHETIC]`, matching the code. `tools/generate_report.py` reads the JSON keys written by `exportReportJson`.
- **Blocked:** gameplay benchmark (no GD 2.2081 client, Geode runtime, or test device in this environment). No FPS claim is made.
- **Not verified:** Geode-only files (`src/geode/Hooks.cpp`, `OptiGDMod.hpp`) are not compiled here. The `m_objects` `count()`/`objectAtIndex()` calls are UNVERIFIED against the bindings.
- **Branch decision:** the session is fixed to `arena/21ca20f2-optigd`. The planned `optigd/v0.1.0-beta1` branch is NOT created. The PR is opened from `arena/21ca20f2-optigd` into `main`.

---

## Entry 5 — Deviations from Entry 3 and open decisions

Entry 3 is kept unchanged as the design of record. This entry lists where the implementation differs.

1. **AdaptiveGovernor sustain windows.** Entry 3 specifies separate sustain windows for downgrade and upgrade. The implementation makes one tier step per decision, requires at least 30 samples, and uses a cooldown after each change. It does not implement separate sustain windows. Reason: the window and cooldown already give hysteresis, and a second window has no measured basis. Revisit only with runtime evidence.
2. **FallbackManager recovery.** Entry 3 specifies restoring a saved baseline. The implementation uses level-based restore: each level has a defined, reversible set of changes applied to the effective config. Reason: a stored snapshot can drift from the user's settings, and levels are easier to test.
3. **Version and GD strings.** Entry 3 specifies runtime `Loader::getVersion()` and `GEODE_GD_VERSION_STRING`. The implementation uses compile-time constants in `src/core/Version.hpp`. Reason: the native build has no Geode loader, and the constants keep the core testable. Runtime-reported version is not compared against the constants; this is a gap.
4. **Spike threshold.** The previous value was a fixed constant of 33333 us. It now derives from the governor's target FPS (2 budgets). At 60 FPS the value is unchanged.
5. **Compatibility.** Entry 3 implies conflict awareness. The implementation has a feature registry and manual conflict reporting only. No automatic detection of other mods.
6. **Mac and iOS platforms.** Removed from `mod.json` (they are not targeted or verified).

### Open decisions for the maintainer (not yet resolved)

- Geode pin: keep `5.10.1` (current) or move to `5.11.1` (latest as of 2026-10-10).
- Tier as a ceiling: should the user's selected tier cap the adaptive governor's upgrades? Current behaviour: the governor moves between tiers in both directions, but never writes to user settings.
- GD 2.2081 client and test device availability (blocks the benchmark).
- Upstream policy: the Geode repository `AGENTS.md` states that AI-written mods are not accepted by the Geode index. This conflicts with the requested work; it must be resolved before any index submission.
- Repository license: no LICENSE file is present.
- Tag spelling: `v0.1.0-beta1` (git) vs `v0.1.0-beta.1` (`mod.json`). Current choice: the tag keeps the requested name; `mod.json` follows Geode's grammar.


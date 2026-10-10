# OptiGD v0.1.0-beta1 — Evidence Report

**Release identifier:** `v0.1.0-beta1` (git tag / prose). **Geode version:** `v0.1.0-beta.1` (`mod.json`).
**Native version:** `0.1.0` (`CMakeLists.txt`). **Target game:** Geometry Dash 2.2081.
**Targets:** Windows x64, Android32, Android64. **Geode SDK:** 5.10.1 (`mod.json`).

This report records what was checked, how, and what was not. It supersedes the v1.0.0 report, whose
simulated speed-up figures were not measurements and have been removed.

---

## 1. Status legend

| Status | Meaning |
|---|---|
| **VERIFIED** | Behaviour specified for the module is checked by automated tests that pass in this repository. Says nothing about the game. |
| **PARTIAL** | Some behaviour is verified, and part of the path is implemented but not checked (usually the game-facing part). |
| **DISABLED** | Code exists, but it is off by default, and/or it is not connected to gameplay in this release. |
| **UNVERIFIED** | Claimed behaviour has no automated test that passes and no runtime evidence. |

Each row also states **Game connection** (what the game actually runs) and **Runtime evidence** (what
has been observed in Geometry Dash). No row has runtime evidence yet.

Build labels: **BUILD-VERIFIED** = the artifact was produced by CI. **RUNTIME-UNVERIFIED** = the
artifact was not run in the game.

---

## 2. Module table

| # | Module | Status | Default | Game connection | Native evidence | Missing before it can be stronger |
|---|---|---|---|---|---|---|
| 1 | `core/MemoryArena` | VERIFIED | ON (counters only) | Per-frame counters; no game allocation is redirected | 4 tests incl. alignment and overflow regressions | Nothing for the stated scope. Not used as an allocator for game objects. |
| 2 | `core/SpatialHash` | VERIFIED | ON (inside culler) | Used by `VisibilityCuller` | 4 tests incl. moved-item and non-finite regressions | Performance at 100k+ objects on device (protocol) |
| 3 | `optimization/VisibilityCuller` | PARTIAL | Shadow only (`enable-culling` OFF) | `OptiPlayLayer::postUpdate` runs a shadow pass when enabled; **no object is hidden or skipped** | 3 tests (counts, update, unregister) | (a) `m_objects` enumeration is UNVERIFIED against bindings (see §4); (b) camera-rectangle conversion is UNVERIFIED in-game |
| 4 | `optimization/ParticleGovernor` | DISABLED | OFF | **Not connected** to any particle system | 5 tests (critical never throttled, hysteresis band, no wake burst, incremental totals, budget) | Connection to `CCParticleSystem` (win/android link) and in-game sleep/wake verification |
| 5 | `optimization/TriggerOptimizer` | DISABLED | OFF | **Not connected**. Model only: it does not reproduce GD's trigger pipeline (`checkCollisions`, `processCommands`) | 4 tests (original, re-arm on restart, prepare-after-register, spawn groups) | Equivalence study against GD trigger semantics; then connection |
| 6 | `optimization/MatrixCache` | DISABLED | OFF | **Not connected**. Cocos2d already caches transforms (`m_bTransformDirty`) | 3 tests (dirty invalidates, input change misses, affine composition) | Evidence that it is faster than cocos' own caching |
| 7 | `adaptive/AdaptiveGovernor` | PARTIAL | ON | Chooses the **effective** tier from frame intervals each frame; effective tuning feeds the culler padding and the (disabled) particle governor | 3 tests (original downgrade, never mutates user config, invalid samples and Custom) | Runtime effect on frame time is UNVERIFIED (protocol). Sustain-window hysteresis from the design log is not implemented; see DESIGN_LOG Entry 5. |
| 8 | `adaptive/FallbackManager` | PARTIAL | ON (via governor) | Level applied to the effective config each frame | 3 tests (escalate and recover, user override, original jitter trigger) | Runtime effect UNVERIFIED |
| 9 | `profiling/Profiler` | VERIFIED | ON | **Not used by hooks** | 1 test (scopes do not collide, macro regression) | Used in hooks only if a later release needs it |
| 10 | `diagnostics/DiagnosticsEngine` | VERIFIED | ON (in memory) | Fed each frame by the manager; spike threshold = 2 frame budgets at target FPS; JSON export | 2 tests (summary from intervals, zero intervals excluded, spikes; spike threshold follows target FPS) | Nothing for the stated scope |
| 11 | `diagnostics/FrameTimeRecorder` | PARTIAL | OFF (`record-frametimes`) | Writes CSV in the save directory when enabled | 1 test (header and row format) | Real in-game file write on Windows and Android is UNVERIFIED |
| 12 | `ui/DiagnosticsOverlay` | DISABLED | OFF | **Not drawn.** Text formatting exists, no rendering hook | Formatting not tested | A drawing hook and a visual check |
| 13 | `config/ConfigManager`, `Presets`, `SettingsMapping` | VERIFIED | — | Settings read through Geode listeners (`syncSettings`) | 6 tests (round-trip, garbage input, presets, unknown tier warning, flag mapping and clamps, default flags) | Settings UI behaviour in-game UNVERIFIED |
| 14 | `compatibility/CompatibilityLayer` | PARTIAL | — | Initialised with real GD/Geode version constants; **no automatic conflict detection** (only manual `reportConflict`) | 1 test (versions passed through, conflict disables feature) | Conflict detection against other mods (not implemented) |
| 15 | `platform/Platform` | PARTIAL | — | Used for timing and system info | Compiled and used natively (Linux). Windows and Android code paths compiled by CI only | Device run |
| 16 | `geode/Hooks.cpp` — `PlayLayer::init/resetLevel/onQuit/postUpdate`, `MenuLayer::init`, settings listeners | BUILD-VERIFIED (compile/link); runtime UNVERIFIED | — | Written against bindings `GeometryDash.bro` (PlayLayer `[[link(android)]]`, win/imac/m1/ios addresses) | CI: the mod target links on Win64 and Android32/64 (the `.geode` file exists only after linking, so `Hooks.cpp` compiled) | In-game run: hooks fire, level lifecycle, settings listeners |
| 17 | Object enumeration `m_objects->count()/objectAtIndex()` in `registerShadowObjects` | BUILD-VERIFIED (compile/link); runtime UNVERIFIED | — | Used only when `enable-culling` is ON | CI compile succeeded on all three targets. The bindings file comments these methods out, so they come from the Geode/cocos headers. Semantics in-game are unverified. | In-game run with culling on |
| 18 | Benchmark suite (`optigd_benchmark`) | VERIFIED (harness runs) / **SYNTHETIC** | — | Host-only; not gameplay | Runs on Linux host | Not evidence for any gameplay claim |
| 19 | Frame-time analyzer `tools/analyze_frametimes.py` | VERIFIED (tool tests) | — | Offline | 12 tool tests on **synthetic CSV** | Real captures from the protocol |

---

## 3. Build and test results

Recorded in this sandbox (Debian 12, GCC 12.2, CMake 4.4.4, Ninja 1.13.2, 2 vCPU). **[MEASURED]**

| Check | Result |
|---|---|
| Native tests, Release (`optigd_tests`) | 39/39 passed |
| Native tests, Debug + AddressSanitizer + UBSan + `-Wall -Wextra -Wpedantic` | 39/39 passed, no sanitizer report, no compiler warning |
| Mutation check A: disable `MatrixCache::markDirty` invalidation | `MatrixCache dirty invalidates` FAILED (test caught it) |
| Mutation check B: remove `TriggerOptimizer` re-arm on `prepareLevel` | `TriggerOptimizer re-arms on restart` FAILED (test caught it) |
| Source restored after mutation checks | 39/39 passed again |
| Tool tests (`python3 -m unittest discover -s tools/tests`) | 12/12 passed (**synthetic CSV, tool tests only**) |
| Geode build, Windows x64 (`build.yml`, run 38042140564 on this branch) | **BUILD-VERIFIED**: job green; `.geode` present and non-empty |
| Geode build, Android 32-bit (same run) | **BUILD-VERIFIED**: job green |
| Geode build, Android 64-bit (same run) | **BUILD-VERIFIED**: job green. Combined `.geode` packaging green |
| Runtime in Geometry Dash, all targets | **RUNTIME-UNVERIFIED** |
| Compiler warnings in CI | Not checked: CI job logs are not accessible from the sandbox. Re-check in the GitHub UI before merge. |

### Defects found before changes and their regression coverage

| Defect (Entry 2) | Fix | Regression test |
|---|---|---|
| SpatialHash `update()` did not reindex | Exact span per item, remove/reinsert on update | SpatialHash moved item |
| MemoryArena accepted non-power-of-two alignment | Validated | MemoryArena rejects bad alignment |
| MemoryArena overflow path miscounted usage | Overflow-safe sizing, `kMaxBlockBytes` guard | MemoryArena rejects overflowing size |
| MatrixCache hit after `markDirty` | Generation/valid check; `invalidate` on destruction | MatrixCache dirty invalidates |
| Triggers never re-arm after restart | `prepareLevel` resets state | TriggerOptimizer re-arms on restart |
| Pointers into trigger vector dangled after registration | Registration invalidates the index | TriggerOptimizer requires prepare after registration |
| `ConfigManager::getConfig` returned unlocked reference; `stoi`/`stof` could throw | Copy under lock; non-throwing parser | ConfigManager rejects garbage |
| Hard-coded `initialize("2.2081","v4.0.0")` | Versions from `Version.hpp` | CompatibilityLayer conflict and defaults |
| `syncSettings` wrote back a stale config (undid presets) | Snapshot → `resolveSettings` → one write | SettingsMapping flags and clamps |
| Governor wrote preset values into the user config | Effective config only | AdaptiveGovernor never mutates user config |
| Fallback permanently reduced settings, no recovery | Reversible levels on the effective config | FallbackManager escalates and recovers |
| Frame time measured only `postUpdate` | Frame interval measured between frames; game update time separate | Diagnostics summary from intervals |
| `heapAllocationsAvoided` mislabelled | Renamed to `arenaAllocations` | Diagnostics summary (counts arena allocations) |
| ParticleGovernor O(N) and hard-coded margin | Incremental totals; hysteresis margins | Incremental totals; hysteresis band |
| `OPTIGD_PROFILE_SCOPE` used `__LINE__` without pasting | Two-level paste | Profiler scopes do not collide |
| Profiler allocated a `std::string` per record | Transparent lookup | (covered by the profiler test; no allocation assertion) |
| `Platform.cpp` missing includes; `TARGET_OS_IPHONE` without header | Includes added, `NOMINMAX` on Windows | Compiled natively; Windows/Apple branches CI-only |
| Hooks: PlayLayer hooks Windows-only; `updateVisibility` no-op | Guard removed (bindings show Android link); no-op removed | Compile check in CI (not yet run) |
| Stale `sdk: latest` / `@main` in CI | Pinned (see `.github/workflows`) | Workflow review; first CI run pending |

Also corrected: the original tests asserted a `Cosmetic` emitter multiplier of 0.5 and a one-shot
trigger; both assertions are kept.

---

## 4. Known blockers and unverified items

1. **Gameplay benchmark: BLOCKED.** No Geometry Dash 2.2081 client, Geode runtime, or test device in
   this environment. See `docs/GAMEPLAY_BENCHMARK_PROTOCOL.md`. Acceptance criterion
   *"OptiGD improves frame time in Geometry Dash"* is **unresolved**. No FPS or frame-time improvement
   is claimed anywhere in this release.
2. **Geode compile: BUILD-VERIFIED** on Windows x64, Android32, and Android64 by `build.yml` (run 38042140564). Runtime is still UNVERIFIED.
3. **Camera rectangle** (`convertToNodeSpace` of window corners into the object layer) is an assumption
   until checked in-game.
4. **Upstream policy:** the Geode repository's `AGENTS.md` states that AI-written mods are not accepted
   by the Geode index. This conflicts with the task and is raised as a publishing question. It is not
   resolved by this release.

---

## 5. Labelling conventions

- **[MEASURED]**: produced by a recorded run in this repository's environment, with the command given.
- **[SYNTHETIC]**: produced by a host-side harness (microbenchmark or synthetic CSV). Never gameplay.
- **[PROJECTED]**: model-based estimate; the model is stated. None in this release.
- **[UNVERIFIED]**: not measured.

### Host microbenchmark run [SYNTHETIC] [MEASURED on host]

Command: `./build/optigd_benchmark` (Release, GCC 12.2, Linux, 2 logical cores, 3.9 GB RAM).
These isolate OptiGD code with generated inputs. They say nothing about frame time in the game.

| Scenario | Iterations | Mean (µs) | P95 (µs) |
|---|---:|---:|---:|
| VisibilityCuller query (camera window, 50k objects) | 300 | 11.697 | 15.144 |
| MemoryArena 64 × 64 B allocations + reset | 300 | 0.293 | 0.304 |
| ParticleGovernor evaluate 2000 emitters | 300 | 26.297 | 29.014 |
| AdaptiveGovernor recordFrameInterval + update | 300 | 1.485 | 2.992 |

Host numbers vary between machines and runs; they are not a claim about any device.

---

## 6. Test environment **[MEASURED]**

- OS: Debian GNU/Linux 12 (bookworm), Linux x86_64; 2 logical cores; ~3.9 GB RAM.
- Compiler: GCC 12.2.0, `-std=c++20`. Build: CMake 4.4.4, Ninja 1.13.2 (Python venv).
- No Geometry Dash client, no Geode runtime, no Android NDK, no Windows host.

## 7. Known limitations (beta)

- Most optimization modules are not connected to gameplay in this release. Their status is DISABLED or
  PARTIAL in §2, and they are off by default.
- Shadow culling measures object-frames a culler would cull. It does not reduce any work.
- The trigger optimizer is a model and not GD's trigger semantics.
- No conflict detection with other mods.
- The diagnostics HUD is not drawn in-game.

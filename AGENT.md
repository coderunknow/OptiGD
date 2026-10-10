# AGENT.md — OptiGD working conventions

These rules apply to every automated or human change in this repository.

## Scope and honesty
- Never fabricate measurements. A performance number is only reported when it was produced by a recorded, reproducible run (see `docs/GAMEPLAY_BENCHMARK_PROTOCOL.md`). Host-side or synthetic numbers are labelled `[SYNTHETIC]`.
- Never weaken an acceptance criterion silently. If a requirement cannot be verified in the environment, leave it explicitly `UNVERIFIED`/`BLOCKED` in `EVIDENCE_REPORT.md` and in the PR.
- A module is only described as "integrated" when it is connected to a real runtime path that is exercised by the build. Tests of a class in isolation are not integration evidence.
- Gameplay-altering optimizations default to OFF unless there is evidence for their default. Document the status of every feature.
- Do not claim a platform works at runtime because it compiles. Build artifacts are "BUILD-VERIFIED"; runtime behaviour is "RUNTIME-UNVERIFIED" until tested on the game.

## Versioning
- Release identifier: `v0.1.0-beta1` (git tag / prose).
- `mod.json` uses Geode's version grammar, which requires a dot before the prerelease number: `v0.1.0-beta.1`. `0.1.0-beta1` is rejected by Geode's `VersionInfo::parse`.
- `CMakeLists.txt` uses a numeric-only `project(VERSION)` (`0.1.0`). Do not put prerelease suffixes there.
- Keep `src/core/Version.hpp` in sync with the above.

## Geode / Geometry Dash
- Target GD version: 2.2081 (see `CMakeLists.txt` `GEODE_GD_VERSION` block and the bindings used by CI).
- Geode SDK pinned in CI to the version in `mod.json` (`v5.10.1`). Do not use `sdk: latest` for verification builds.
- Any Geometry Dash member function used from a hook must be checked against the bindings (`geode-sdk/bindings`, `bindings/2.2081`) for the platforms it claims.
- Hooks must be compiled for Win64, Android32 and Android64. Do not wrap hooks in platform guards unless the bindings show no address/symbol for that platform.

## Build and validation
- Native core: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/optigd_tests`.
- Python tooling: `python3 -m unittest discover -s tools/tests`.
- Geode artifacts: produced only by `.github/workflows/build.yml`; a job is green only if its verification steps passed (exact `.geode` contents checked).
- Never commit build outputs, `.geode` files, game binaries, game assets, or captured personal data. Benchmark raw data is kept out of Git unless small and explicitly requested.

## Repository hygiene
- `DESIGN_LOG.md` is append-only. Add entries; never rewrite earlier ones.
- Do not rewrite Git history, create tags, publish releases, or merge pull requests without an explicit instruction from the user.
- Language: prose in English. `docs/RELEASE_WORKFLOW.md` is Vietnamese and stays Vietnamese.

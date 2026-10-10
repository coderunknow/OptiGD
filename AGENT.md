# OptiGD Agent & Engineering Conventions

## 1. Project Identity & Target Metadata
- **Project**: OptiGD (`coderunknow.optigd`)
- **Current Target Version**: `v0.1.0-beta.2`
  - `mod.json`: `"version": "v0.1.0-beta.2"`, `"geode": "5.10.1"`
  - `CMakeLists.txt`: `project(OptiGD VERSION 0.1.0 LANGUAGES CXX)` (CMake `project(... VERSION ...)` requires numeric `MAJOR.MINOR.PATCH`; full prerelease tag `0.1.0-beta.2` is defined in `src/core/Types.hpp` and `mod.json`).
  - The prerelease form is load-bearing: Geode `5.10.1`'s `VersionInfo::parse` **rejects** `v0.1.0-beta2` (`Expected end of version, found '2'`) and only accepts `v0.1.0-beta.2`. `src/core/GeodeVersion.cpp` ports that grammar, `tools/validate_geode_metadata.py` gatekeeps it in CI, and `tests/UnitTests.cpp::testGeodeVersionMetadataGrammar` locks it down.
- **Target Game Version**: Geometry Dash `2.2081`
- **Required Build Targets**:
  - Windows x64 (`Win64` -> `coderunknow.optigd.dll`)
  - Android 32-bit (`Android32` -> `coderunknow.optigd.android32.so`)
  - Android 64-bit (`Android64` -> `coderunknow.optigd.android64.so`)

## 2. Build & Verification Commands
### Native Standalone Build, Tests, and Synthetic Benchmarks
```bash
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/optigd_tests
./build/optigd_benchmark
```

### Multi-Platform Geode Mod Build (`.geode`)
- Built in CI via `.github/workflows/build.yml` using `geode-sdk/build-geode-mod` with `sdk: given` (matching `mod.json`).
- Every matrix job (`Win64`, `Android32`, `Android64`) and the combined package job must verify that the output `.geode` archive is non-empty and contains the expected platform binary and `mod.json`.

## 3. Engineering & Reality-Grounding Rules
1. **No Fabricated Measurements**:
   - Label all metrics explicitly as `[MEASURED]`, `[SIMULATED]`, `[PROJECTED]`, or `[UNVERIFIED]`.
   - Never represent synthetic C++ microbenchmarks (`optigd_benchmark`) as real Geometry Dash client frame-time measurements.
   - If a real Geometry Dash client or hardware target is unavailable in the execution environment, leave the real-gameplay benchmark acceptance criterion explicitly **BLOCKED / UNVERIFIED** and open a **Draft PR** rather than claiming a validated release.
2. **No Silently Weakened Acceptance Criteria**:
   - Do not stub out gameplay hooks behind platform guards (such as `#if defined(GEODE_IS_WINDOWS)`) while claiming multi-platform support.
   - Aggressive optimizations that could alter vanilla level semantics (such as replacing Geometry Dash's native trigger execution) must be constrained behind safe defaults and explicit configuration flags.
3. **Timing & Diagnostics Integrity**:
   - Distinguish inter-frame interval (`totalFrameTimeUs`) from hooked CPU update time (`updateCpuTimeUs`) and OptiGD's own CPU cost (`optigdOverheadUs`).
   - Do not claim CPU update timing measures GPU rendering time.
   - All telemetry and diagnostics must remain 100% local (zero network calls).
4. **Design Log Maintenance**:
   - Maintain `DESIGN_LOG.md` as an append-only engineering log recording findings, design decisions, implementation changes, verification evidence, and unresolved blockers.

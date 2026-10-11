# OptiGD `v0.1.0-beta.3` CI/CD & Release Workflow

## 1. Continuous Integration (`.github/workflows/build.yml`)

Every push and pull request runs a three-stage verification pipeline:

1. **`standalone-test-and-benchmark` (`ubuntu-latest`, `windows-latest`)**:
   - Compiles `optigd_core`, `optigd_tests`, and `optigd_benchmark` in `Release` mode (`-DOPTIGD_STANDALONE_ONLY=ON`).
   - Runs `ctest --output-on-failure --verbose` — **6 checks**: `OptiGDUnitTests` (50 test functions), `OptiGDDiagnosticsJson`, `OptiGDGeodeMetadataValidation`, `OptiGDBenchmarkSessionJson`, `OptiGDExternalComparisonTool`, `OptiGDBenchmarkJsonSchema`.
   - Executes `optigd_benchmark` and validates `benchmark_results.json` metadata (`optigd_version == "0.1.0-beta.3"`, `evidence_classification == "[SIMULATED]"`).
   - `OptiGDBenchmarkSessionJson` runs `optigd_tests --emit-sessions`, which drives the real `BenchmarkController` with a synthetic clock into a temporary directory, then validates the emitted session records and their comparison with `tools/validate_benchmark_session_json.py`. Every emitted record must be `[SIMULATED]`: the standalone binary cannot claim an in-game capture.

2. **`build-geode-mod` (`Win64`, `Android64`, `Android32`)**:
   - Uses `geode-sdk/build-geode-mod@main` with `sdk: given` (which reads `"geode": "5.10.1"` from `mod.json` and installs the matching target binaries).
   - Verifies each built `.geode` archive with `unzip -l` and `jq`, failing the job if the platform binary (`coderunknow.optigd.dll`, `coderunknow.optigd.android64.so`, or `coderunknow.optigd.android32.so`) or `mod.json` (`"version": "v0.1.0-beta.3"`) is missing.

3. **`combine`**:
   - Merges the platform `.geode` packages via `geode-sdk/build-geode-mod/combine@main`.
   - Verifies that the combined `coderunknow.optigd.geode` archive contains **all three** platform binaries (`.dll`, `.android64.so`, `.android32.so`) and `mod.json` (`v0.1.0-beta.3`).
   - Uploads the verified multi-platform `coderunknow.optigd.geode` artifact.

---

## 2. Tagged Release Workflow (`.github/workflows/release.yml`)

When a version tag (`v*`, such as `v0.1.0-beta.3`) is pushed after human approval:
1. Runs the pre-release unit test and benchmark gate.
2. Builds and verifies the `Win64`, `Android64`, and `Android32` `.geode` packages.
3. Combines and verifies the multi-platform `coderunknow.optigd.geode` package.
4. Automatically detects pre-release tags containing `-` (e.g., `-beta.3`, `-rc1`) and sets `prerelease: true` on the GitHub Release.

No tag was pushed and no release was published for `v0.1.0-beta.3`; the release
workflow is documented here because it exists, not because it ran.

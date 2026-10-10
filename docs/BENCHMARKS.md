# Benchmarks (v0.1.0-beta1)

**There are no gameplay benchmark results in this release.** The v1.0.0 benchmark tables were simulated
and have been removed; they were not measurements of Geometry Dash.

## What exists

| Harness | Label | Purpose | Gameplay evidence? |
|---|---|---|---|
| `optigd_benchmark` (`benchmarks/main.cpp`) | **[SYNTHETIC]** | Host-side timing of isolated OptiGD functions with generated inputs | No |
| `tools/analyze_frametimes.py` | Tool | Summarizes and compares frame-time CSVs from real runs | Only when fed real captures |
| `tools/tests` | Tool tests | Checks the analyzer on synthetic CSVs | No |

Run the microbenchmarks:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/optigd_benchmark
```

The current host-run figures are in `EVIDENCE_REPORT.md` §5, labelled [SYNTHETIC].

## What is required for a gameplay claim

The protocol in [`GAMEPLAY_BENCHMARK_PROTOCOL.md`](GAMEPLAY_BENCHMARK_PROTOCOL.md) must be executed on a real
GD 2.2081 client, with the acceptance criteria in §6 of that document. The status is **BLOCKED** (see the
protocol §8).

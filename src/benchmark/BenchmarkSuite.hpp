#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace optigd::benchmark {

// One host-side microbenchmark. Every value is a MEASURED time on the machine that ran it.
// These are SYNTHETIC: they measure OptiGD code in isolation with generated inputs. They
// are not gameplay measurements and must never be reported as FPS improvements.
struct MicroResult {
    std::string name;
    size_t iterations{0};
    double meanUs{0.0};
    double p95Us{0.0};
};

// Runs the microbenchmarks. Sizes are inputs; results are whatever this host measures.
std::vector<MicroResult> runMicrobenchmarks(size_t objectCount = 50000, size_t iterations = 300);

// Markdown table labelled [SYNTHETIC], with host info.
std::string formatReport(const std::vector<MicroResult>& results);

} // namespace optigd::benchmark

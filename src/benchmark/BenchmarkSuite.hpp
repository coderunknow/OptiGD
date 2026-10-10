#pragma once

#include "../core/Types.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace optigd::benchmark {

struct BenchmarkResult {
    std::string scenarioName;
    size_t objectCount{0};
    size_t emitterCount{0};
    size_t triggerCount{0};
    size_t framesSimulated{0};

    // Baseline (Unoptimized O(N) traversal + heap allocations)
    double baselineMeanUs{0.0};
    double baselineMedianUs{0.0};
    double baselineP95Us{0.0};
    double baselineP99Us{0.0};
    uint64_t baselineAllocations{0};

    // OptiGD (Spatial Hash + Particle Governor + Trigger Indexer + Arena)
    double optigdMeanUs{0.0};
    double optigdMedianUs{0.0};
    double optigdP95Us{0.0};
    double optigdP99Us{0.0};
    uint64_t optigdHeapAllocations{0};
    uint64_t optigdArenaAllocations{0};

    double speedupFactor{1.0};
    double cpuReductionPercent{0.0};
};

class BenchmarkSuite {
public:
    static std::vector<BenchmarkResult> runAllBenchmarks();
    static BenchmarkResult runScenario(const std::string& name,
                                       size_t numObjects,
                                       size_t numEmitters,
                                       size_t numTriggers,
                                       size_t numFrames);

    static std::string formatMarkdownReport(const std::vector<BenchmarkResult>& results);
    static std::string formatJsonReport(const std::vector<BenchmarkResult>& results);
};

} // namespace optigd::benchmark

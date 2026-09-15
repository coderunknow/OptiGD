#pragma once

#include "../core/Types.hpp"
#include <string>
#include <vector>

namespace optigd::benchmark {

struct BenchmarkMetricResult {
    std::string name;
    double baselineMeanUs{0.0};
    double optigdMeanUs{0.0};
    double speedupFactor{1.0};

    double baselineP95Us{0.0};
    double optigdP95Us{0.0};

    double baselineP99Us{0.0};
    double optigdP99Us{0.0};

    double baselineStdDevUs{0.0};
    double optigdStdDevUs{0.0};

    double baselineSimulatedFps{0.0};
    double optigdSimulatedFps{0.0};

    std::string notes;
};

struct BenchmarkSuiteResult {
    std::string testEnvironment;
    std::vector<BenchmarkMetricResult> scenarios;
    double adaptiveGovernorOverheadUs{0.0};
    double memoryArenaAllocationSpeedup{0.0};
};

class BenchmarkSuite {
public:
    static BenchmarkSuiteResult runAllBenchmarks(size_t objectCount = 50000, size_t frameCount = 300);

    static BenchmarkMetricResult runCullingBenchmark(size_t objectCount, size_t frameCount);
    static BenchmarkMetricResult runMemoryBenchmark(size_t allocsPerFrame, size_t frameCount);
    static BenchmarkMetricResult runParticleBenchmark(size_t emitterCount, size_t frameCount);
    static BenchmarkMetricResult runTriggerBenchmark(size_t triggerCount, size_t frameCount);
    static BenchmarkMetricResult runIntegratedBenchmark(size_t objectCount, size_t particleCount, size_t triggerCount, size_t frameCount);

    static double measureGovernorOverhead(size_t iterations = 10000);

    static void printResultsTable(const BenchmarkSuiteResult& result);
    static std::string exportMarkdownReport(const BenchmarkSuiteResult& result);
};

} // namespace optigd::benchmark

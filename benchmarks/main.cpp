#include "../src/benchmark/BenchmarkSuite.hpp"
#include "../src/core/Types.hpp"
#include <iostream>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    std::cout << "==========================================================\n";
    std::cout << "  OptiGD v" << optigd::core::kOptiGDVersion << " Synthetic Benchmark Suite [SIMULATED]\n";
    std::cout << "==========================================================\n\n";

    auto results = optigd::benchmark::BenchmarkSuite::runAllBenchmarks();
    std::string mdReport = optigd::benchmark::BenchmarkSuite::formatMarkdownReport(results);
    std::cout << mdReport << "\n";

    std::string jsonPath = "benchmark_results.json";
    if (argc > 1) {
        jsonPath = argv[1];
    }

    std::ofstream ofs(jsonPath);
    if (ofs.is_open()) {
        ofs << optigd::benchmark::BenchmarkSuite::formatJsonReport(results);
        std::cout << "Exported structured JSON benchmark results to: " << jsonPath << "\n";
    }

    return 0;
}

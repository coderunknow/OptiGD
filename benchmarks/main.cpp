#include "../src/benchmark/BenchmarkSuite.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    size_t objects = 50000;
    size_t frames = 300;

    std::cout << "Starting OptiGD Synthetic Benchmark Suite...\n";
    auto result = optigd::benchmark::BenchmarkSuite::runAllBenchmarks(objects, frames);

    std::cout << "Benchmark run completed successfully.\n";
    return 0;
}

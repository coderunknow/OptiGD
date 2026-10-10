#include "../src/benchmark/BenchmarkSuite.hpp"

#include <iostream>

int main() {
    std::cout << "OptiGD synthetic microbenchmarks [SYNTHETIC]. Not gameplay measurements.\n";
    const auto results = optigd::benchmark::runMicrobenchmarks(50000, 300);
    std::cout << optigd::benchmark::formatReport(results);
    return 0;
}

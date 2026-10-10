#include "BenchmarkSuite.hpp"

#include "../adaptive/AdaptiveGovernor.hpp"
#include "../core/MemoryArena.hpp"
#include "../core/Types.hpp"
#include "../optimization/ParticleGovernor.hpp"
#include "../optimization/VisibilityCuller.hpp"
#include "../platform/Platform.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

namespace optigd::benchmark {

namespace {

// Times fn() once per iteration and returns the per-call distribution in microseconds.
template <typename Fn>
MicroResult timeIterations(const std::string& name, size_t iterations, Fn&& fn) {
    std::vector<double> samples;
    samples.reserve(iterations);
    for (size_t i = 0; i < iterations; ++i) {
        const uint64_t t0 = platform::Platform::getTimestampNs();
        fn(i);
        const uint64_t t1 = platform::Platform::getTimestampNs();
        samples.push_back(static_cast<double>(t1 - t0) / 1000.0);
    }
    MicroResult r;
    r.name = name;
    r.iterations = iterations;
    double sum = 0.0;
    for (double s : samples) sum += s;
    r.meanUs = samples.empty() ? 0.0 : sum / static_cast<double>(samples.size());
    if (!samples.empty()) {
        std::sort(samples.begin(), samples.end());
        size_t idx = static_cast<size_t>(0.95 * static_cast<double>(samples.size()));
        if (idx >= samples.size()) idx = samples.size() - 1;
        r.p95Us = samples[idx];
    }
    return r;
}

} // namespace

std::vector<MicroResult> runMicrobenchmarks(size_t objectCount, size_t iterations) {
    std::vector<MicroResult> results;
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> posX(0.0f, 100000.0f);
    std::uniform_real_distribution<float> posY(0.0f, 2000.0f);
    std::uniform_real_distribution<float> size(8.0f, 40.0f);

    // 1. Spatial index: query a camera-sized window over objectCount objects.
    {
        optimization::VisibilityCuller culler(128.0f);
        culler.setPadding(120.0f, 80.0f);
        culler.reserve(objectCount);
        std::vector<void*> tokens(objectCount);
        for (size_t i = 0; i < objectCount; ++i) {
            tokens[i] = reinterpret_cast<void*>(static_cast<uintptr_t>(i + 1));
            culler.registerObject(core::Rect2D(posX(rng), posY(rng), size(rng), size(rng)), tokens[i]);
        }
        std::vector<void*> visible;
        visible.reserve(objectCount);
        results.push_back(timeIterations("VisibilityCuller query (camera window)", iterations, [&](size_t i) {
            const float x = static_cast<float>((i * 977) % 100000);
            culler.cullAgainstCamera(core::Rect2D(x, 900.0f, 800.0f, 600.0f), visible);
        }));
    }

    // 2. Arena: 64 small allocations per iteration, then reset.
    {
        core::MemoryArena arena(256 * 1024);
        results.push_back(timeIterations("MemoryArena 64 x 64B allocations + reset", iterations, [&](size_t) {
            for (int k = 0; k < 64; ++k) {
                (void)arena.allocate(64, 16);
            }
            arena.reset();
        }));
    }

    // 3. Particle governor: evaluate 2000 emitters against a camera rectangle.
    {
        optimization::ParticleGovernor gov(0.5f, 4096);
        std::vector<uint32_t> handles;
        handles.reserve(2000);
        for (int i = 0; i < 2000; ++i) {
            handles.push_back(gov.registerEmitter(core::Point2D(posX(rng), posY(rng)), 32.0f,
                                                  optimization::ParticlePriority::Cosmetic));
        }
        results.push_back(timeIterations("ParticleGovernor evaluate 2000 emitters", iterations, [&](size_t i) {
            const float x = static_cast<float>((i * 977) % 100000);
            const core::Rect2D cam(x, 900.0f, 800.0f, 600.0f);
            for (auto h : handles) {
                (void)gov.evaluateEmitter(h, cam);
            }
        }));
    }

    // 4. Adaptive governor: one frame update with a full window.
    {
        core::OptiConfig cfg{};
        adaptive::AdaptiveGovernor gov(cfg);
        for (int i = 0; i < 200; ++i) gov.recordFrameInterval(16666.0 + (i % 7) * 100.0);
        results.push_back(timeIterations("AdaptiveGovernor recordFrameInterval + update", iterations, [&](size_t i) {
            gov.recordFrameInterval(16666.0 + static_cast<double>(i % 500));
            gov.update(cfg);
        }));
    }

    return results;
}

std::string formatReport(const std::vector<MicroResult>& results) {
    const auto sys = platform::Platform::getSystemInfo();
    std::ostringstream ss;
    ss << "# OptiGD synthetic microbenchmarks [SYNTHETIC]\n\n"
       << "Host: " << sys.osName << ", " << sys.logicalCores << " logical cores, "
       << sys.totalRamMb << " MB RAM, " << sys.compiler << "\n\n"
       << "These measure isolated OptiGD code with generated inputs on this host.\n"
       << "They are NOT gameplay measurements and do not show any FPS change.\n\n"
       << "| Scenario | Iterations | Mean (us) | P95 (us) |\n"
       << "|---|---:|---:|---:|\n";
    ss << std::fixed << std::setprecision(3);
    for (const auto& r : results) {
        ss << "| " << r.name << " | " << r.iterations << " | " << r.meanUs << " | " << r.p95Us << " |\n";
    }
    return ss.str();
}

} // namespace optigd::benchmark

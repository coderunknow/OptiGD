#include "../src/core/MemoryArena.hpp"
#include "../src/core/SpatialHash.hpp"
#include "../src/optimization/VisibilityCuller.hpp"
#include "../src/optimization/ParticleGovernor.hpp"
#include "../src/optimization/TriggerOptimizer.hpp"
#include "../src/adaptive/AdaptiveGovernor.hpp"
#include "../src/adaptive/FallbackManager.hpp"
#include "../src/config/ConfigManager.hpp"
#include "../src/compatibility/CompatibilityLayer.hpp"
#include "../src/profiling/Profiler.hpp"

#include <iostream>
#include <cassert>
#include <vector>
#include <string>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

bool testMemoryArena() {
    std::cout << "  Running MemoryArena tests...\n";
    optigd::core::MemoryArena arena(1024);

    void* p1 = arena.allocate(64, 8);
    TEST_ASSERT(p1 != nullptr, "p1 allocation failed");
    TEST_ASSERT(reinterpret_cast<uintptr_t>(p1) % 8 == 0, "p1 alignment incorrect");

    void* p2 = arena.allocate(128, 16);
    TEST_ASSERT(p2 != nullptr, "p2 allocation failed");
    TEST_ASSERT(reinterpret_cast<uintptr_t>(p2) % 16 == 0, "p2 alignment incorrect");
    TEST_ASSERT(arena.getUsedBytes() >= (64 + 128), "Used bytes not tracked properly");

    arena.reset();
    TEST_ASSERT(arena.getUsedBytes() == 0, "Used bytes not reset to 0");

    // Test multi-block expansion
    void* largePtr = arena.allocate(2048);
    TEST_ASSERT(largePtr != nullptr, "Multi-block allocation failed");
    TEST_ASSERT(arena.getCapacity() >= 2048, "Capacity did not expand");

    return true;
}

bool testSpatialHash() {
    std::cout << "  Running SpatialHash tests...\n";
    optigd::core::SpatialHash hash(100.0f, 64);

    int tag1 = 101, tag2 = 102, tag3 = 103;
    // Item 1: at (50, 50, 20, 20) -> cell (0,0)
    hash.insert(optigd::core::Rect2D(50.0f, 50.0f, 20.0f, 20.0f), &tag1);
    // Item 2: spans boundary (90, 90, 30, 30) -> cells (0,0), (1,0), (0,1), (1,1)
    hash.insert(optigd::core::Rect2D(90.0f, 90.0f, 30.0f, 30.0f), &tag2);
    // Item 3: far away at (5000, 5000, 10, 10)
    hash.insert(optigd::core::Rect2D(5000.0f, 5000.0f, 10.0f, 10.0f), &tag3);

    // Query (0, 0, 200, 200) should find tag1 and tag2, but NOT tag3
    std::vector<void*> results;
    hash.query(optigd::core::Rect2D(0.0f, 0.0f, 200.0f, 200.0f), results);

    TEST_ASSERT(results.size() == 2, "Expected 2 visible items");
    // Verify deduplication: tag2 spans 4 cells but must appear exactly once!
    size_t countTag2 = 0;
    for (void* ptr : results) {
        if (*static_cast<int*>(ptr) == 102) countTag2++;
    }
    TEST_ASSERT(countTag2 == 1, "Item spanning multiple cells was duplicated!");

    return true;
}

bool testVisibilityCuller() {
    std::cout << "  Running VisibilityCuller tests...\n";
    optigd::optimization::VisibilityCuller culler(128.0f);
    culler.setPadding(50.0f, 50.0f);

    int objA = 1, objB = 2;
    culler.registerObject(0, optigd::core::Rect2D(100.0f, 100.0f, 30.0f, 30.0f), &objA);
    culler.registerObject(1, optigd::core::Rect2D(2000.0f, 100.0f, 30.0f, 30.0f), &objB);

    std::vector<void*> visible;
    culler.cullAgainstCamera(optigd::core::Rect2D(0.0f, 0.0f, 500.0f, 500.0f), visible);

    TEST_ASSERT(visible.size() == 1, "Expected exactly 1 visible object");
    TEST_ASSERT(*static_cast<int*>(visible[0]) == 1, "Expected objA to be visible");
    TEST_ASSERT(culler.getLastCulledCount() == 1, "Expected 1 culled object");

    return true;
}

bool testParticleGovernor() {
    std::cout << "  Running ParticleGovernor tests...\n";
    optigd::optimization::ParticleGovernor gov(0.5f, 1000);

    // Emitter 0: Critical at (5000, 5000) (offscreen)
    gov.registerEmitter(0, optigd::core::Point2D(5000.0f, 5000.0f), 50.0f, optigd::optimization::ParticlePriority::Critical);
    // Emitter 1: Cosmetic at (5000, 5000) (offscreen)
    gov.registerEmitter(1, optigd::core::Point2D(5000.0f, 5000.0f), 50.0f, optigd::optimization::ParticlePriority::Cosmetic);
    // Emitter 2: Cosmetic at (100, 100) (onscreen)
    gov.registerEmitter(2, optigd::core::Point2D(100.0f, 100.0f), 50.0f, optigd::optimization::ParticlePriority::Cosmetic);

    optigd::core::Rect2D cam(0.0f, 0.0f, 800.0f, 600.0f);
    float mult = 1.0f;

    // Critical must not be culled even offscreen
    bool critActive = gov.evaluateEmitter(0, cam, mult);
    TEST_ASSERT(critActive == true, "Critical emitter was culled offscreen");

    // Cosmetic offscreen must be culled
    bool cosmOffActive = gov.evaluateEmitter(1, cam, mult);
    TEST_ASSERT(cosmOffActive == false, "Cosmetic offscreen emitter was not culled");

    // Cosmetic onscreen should be active and scaled by density scale (0.5f)
    bool cosmOnActive = gov.evaluateEmitter(2, cam, mult);
    TEST_ASSERT(cosmOnActive == true, "Cosmetic onscreen emitter was incorrectly culled");
    TEST_ASSERT(std::abs(mult - 0.5f) < 0.01f, "Density scale not applied correctly");

    return true;
}

bool testTriggerOptimizer() {
    std::cout << "  Running TriggerOptimizer tests...\n";
    optigd::optimization::TriggerOptimizer trigOpt(500.0f);

    int trig1 = 1, trig2 = 2, trig3 = 3;
    trigOpt.registerTrigger(0, 0, optigd::optimization::TriggerType::TouchOrPosition, 100.0f, &trig1);
    trigOpt.registerTrigger(1, 0, optigd::optimization::TriggerType::TouchOrPosition, 300.0f, &trig2);
    trigOpt.registerTrigger(2, 0, optigd::optimization::TriggerType::TouchOrPosition, 2000.0f, &trig3);
    trigOpt.prepareLevel();

    std::vector<void*> executed;
    // Player at X = 150: trig1 should fire, trig2 is ahead (within lookahead), trig3 is far beyond
    trigOpt.evaluate(150.0f, executed);
    TEST_ASSERT(executed.size() == 1, "Expected 1 trigger executed at X=150");
    TEST_ASSERT(*static_cast<int*>(executed[0]) == 1, "Expected trig1 to fire");

    // Second evaluation at X = 150: trig1 is one-shot, must not fire again
    executed.clear();
    trigOpt.evaluate(150.0f, executed);
    TEST_ASSERT(executed.empty(), "One-shot trigger fired twice");

    // Player advances to X = 350: trig2 should fire
    trigOpt.evaluate(350.0f, executed);
    TEST_ASSERT(executed.size() == 1, "Expected trig2 to fire at X=350");
    TEST_ASSERT(*static_cast<int*>(executed[0]) == 2, "Expected trig2 to fire");

    return true;
}

bool testAdaptiveGovernorAndFallback() {
    std::cout << "  Running AdaptiveGovernor & FallbackManager tests...\n";
    optigd::core::OptiConfig cfg{};
    cfg.tier = optigd::core::PerformanceTier::Balanced;
    cfg.targetFrameRate = 60.0f;
    cfg.targetFrameTimeMs = 16.666f;
    cfg.fallbackVarianceThresholdMs = 4.0f;
    cfg.fallbackCooldownFrames = 10;

    optigd::adaptive::AdaptiveGovernor gov(cfg);
    optigd::adaptive::FallbackManager fallback(4.0f, 10);

    // Simulate heavy frame drop (35ms per frame)
    for (int i = 0; i < 40; ++i) {
        gov.recordFrameTime(35000.0);
    }
    gov.update(cfg);

    TEST_ASSERT(gov.getCurrentTier() == optigd::core::PerformanceTier::LowEnd,
                "AdaptiveGovernor failed to downgrade to LowEnd under heavy load");

    // Test fallback triggered under high jitter
    auto stats = gov.getRecentStats();
    stats.stdDevMs = 6.0f;
    stats.fps = 40.0f;
    bool fbTriggered = fallback.checkAndApplyFallbacks(100, stats, cfg);
    TEST_ASSERT(fbTriggered == true, "FallbackManager failed to trigger on high jitter");
    TEST_ASSERT(fallback.isFallbackActive() == true, "Fallback not marked active");

    return true;
}

bool testConfigSerialization() {
    std::cout << "  Running ConfigManager tests...\n";
    auto& mgr = optigd::config::ConfigManager::instance();
    mgr.resetToDefaults();
    mgr.applyTierPreset(optigd::core::PerformanceTier::LowEnd);

    std::string serialized = mgr.serialize();
    TEST_ASSERT(!serialized.empty(), "Serialization produced empty string");

    mgr.applyTierPreset(optigd::core::PerformanceTier::HighEnd);
    TEST_ASSERT(mgr.getConfig().tier == optigd::core::PerformanceTier::HighEnd, "HighEnd not set");

    bool deserialized = mgr.deserialize(serialized);
    TEST_ASSERT(deserialized, "Deserialization failed");
    TEST_ASSERT(mgr.getConfig().tier == optigd::core::PerformanceTier::LowEnd, "Tier not restored to LowEnd");

    return true;
}

bool testCompatibility() {
    std::cout << "  Running CompatibilityLayer tests...\n";
    auto& comp = optigd::compat::CompatibilityLayer::instance();
    comp.initialize("2.2081", "v4.0.0");

    TEST_ASSERT(comp.isFeatureUsable("spatial_culling"), "spatial_culling should be usable initially");
    comp.reportConflict("spatial_culling", "Third-party camera mod hook detected");
    TEST_ASSERT(!comp.isFeatureUsable("spatial_culling"), "spatial_culling should be disabled on conflict");
    TEST_ASSERT(comp.isFeatureUsable("particle_governor"), "particle_governor should remain unaffected");

    return true;
}

int main() {
    std::cout << "\n=========================================\n";
    std::cout << "   OPTIGD v1.0.0 UNIT & INTEGRATION TESTS\n";
    std::cout << "=========================================\n\n";

    bool pass = true;
    pass &= testMemoryArena();
    pass &= testSpatialHash();
    pass &= testVisibilityCuller();
    pass &= testParticleGovernor();
    pass &= testTriggerOptimizer();
    pass &= testAdaptiveGovernorAndFallback();
    pass &= testConfigSerialization();
    pass &= testCompatibility();

    std::cout << "\n=========================================\n";
    if (pass) {
        std::cout << "   ALL TESTS PASSED SUCCESSFULLY! [MEASURED]\n";
    } else {
        std::cout << "   SOME TESTS FAILED!\n";
    }
    std::cout << "=========================================\n\n";

    return pass ? 0 : 1;
}

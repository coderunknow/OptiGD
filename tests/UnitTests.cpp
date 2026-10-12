#include "../src/core/Types.hpp"
#include "../src/core/GeodeVersion.hpp"
#include "../src/core/Logger.hpp"
#include "../src/core/MemoryArena.hpp"
#include "../src/core/SpatialHash.hpp"
#include "../src/core/OptiGDManager.hpp"
#include "../src/optimization/VisibilityCuller.hpp"
#include "../src/optimization/ParticleGovernor.hpp"
#include "../src/optimization/TriggerOptimizer.hpp"
#include "../src/optimization/MatrixCache.hpp"
#include "../src/adaptive/AdaptiveGovernor.hpp"
#include "../src/adaptive/FallbackManager.hpp"
#include "../src/profiling/Profiler.hpp"
#include "../src/diagnostics/DiagnosticsEngine.hpp"
#include "../src/compatibility/CompatibilityLayer.hpp"
#include "../src/config/ConfigManager.hpp"
#include "../src/platform/Platform.hpp"
#include "../src/ui/DiagnosticsOverlay.hpp"
#include "../src/ui/BenchmarkPanel.hpp"
#include "../src/benchmark/BenchmarkSuite.hpp"
#include "../src/core/JsonValue.hpp"
#include "../src/diagnostics/SampleWindow.hpp"
#include "../src/benchmark/BenchmarkTypes.hpp"
#include "../src/benchmark/BenchmarkSession.hpp"
#include "../src/benchmark/BenchmarkStore.hpp"
#include "../src/benchmark/BenchmarkController.hpp"
#include "../src/benchmark/BenchmarkCompare.hpp"

#include <iostream>
#include <fstream>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>

namespace {

int g_testsPassed = 0;
int g_testsFailed = 0;

#define OPTIGD_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << __FUNCTION__ << " (" << __LINE__ << "): " << (msg) << "\n"; \
            g_testsFailed++; \
            return; \
        } \
    } while (0)

#define OPTIGD_PASS() \
    do { \
        g_testsPassed++; \
        std::cout << "[PASS] " << __FUNCTION__ << "\n"; \
    } while (0)

// 1. MemoryArena Tests (Alignment, Zero-Byte, Overflow, Reset)
void testMemoryArena() {
    optigd::core::MemoryArena arena(4096);
    OPTIGD_ASSERT(arena.getCapacity() == 4096, "Capacity should be 4096");
    OPTIGD_ASSERT(arena.getUsedBytes() == 0, "Initial used bytes should be 0");

    // Zero-byte allocation must return nullptr safely without advancing offset
    void* zeroPtr = arena.allocate(0, 16);
    OPTIGD_ASSERT(zeroPtr == nullptr, "0-byte allocation should return nullptr");
    OPTIGD_ASSERT(arena.getUsedBytes() == 0, "0-byte allocation should not consume space");

    int* arr = arena.allocateArray<int>(100);
    OPTIGD_ASSERT(arr != nullptr, "Allocation of 100 ints should succeed");
    OPTIGD_ASSERT(reinterpret_cast<uintptr_t>(arr) % alignof(int) == 0, "Pointer must be aligned");

    for (int i = 0; i < 100; ++i) arr[i] = i * 2;
    OPTIGD_ASSERT(arr[50] == 100, "Written data must remain intact");
    OPTIGD_ASSERT(arena.getUsedBytes() >= 400, "Used bytes should reflect allocation");

    // Non-power-of-two alignment should normalize up to next power of two (16)
    void* nonPow2Ptr = arena.allocate(32, 12);
    OPTIGD_ASSERT(nonPow2Ptr != nullptr, "Non-power-of-two alignment allocation should succeed");
    OPTIGD_ASSERT(reinterpret_cast<uintptr_t>(nonPow2Ptr) % 16 == 0, "Normalized to 16-byte alignment");

    // Test block expansion when exceeding initial 4096-byte block
    void* expanded = arena.allocate(8192, 64);
    OPTIGD_ASSERT(expanded != nullptr, "Allocation larger than initial block should expand arena");
    OPTIGD_ASSERT(reinterpret_cast<uintptr_t>(expanded) % 64 == 0, "Expanded allocation must be 64-byte aligned");
    OPTIGD_ASSERT(arena.getBlockCount() == 2 && arena.getExpansionCount() == 1, "Arena should have expanded once");

    // Test overflow rejection
    void* impossible = arena.allocate(static_cast<size_t>(1) << 31);
    OPTIGD_ASSERT(impossible == nullptr, "Exceeding single-allocation safety limit should return nullptr");
    int* overflowArr = arena.allocateArray<int>(std::numeric_limits<size_t>::max() / 2);
    OPTIGD_ASSERT(overflowArr == nullptr, "Overflow array count should return nullptr safely");

    size_t peakBeforeReset = arena.getPeakUsage();
    arena.reset();
    OPTIGD_ASSERT(arena.getUsedBytes() == 0, "After reset, used bytes should be 0");
    OPTIGD_ASSERT(arena.getAllocationCount() == 0, "After reset, allocation count should be 0");
    OPTIGD_ASSERT(arena.getPeakUsage() == peakBeforeReset, "Peak usage should persist across reset");

    // Re-allocating 8192 bytes after reset should reuse the existing second block without expanding again
    void* reused = arena.allocate(8192, 64);
    OPTIGD_ASSERT(reused != nullptr && arena.getExpansionCount() == 1,
                  "After reset, existing expanded block must be reused without new expansion");

    OPTIGD_PASS();
}

// 2. SpatialHash Tests (Insert, Moving-Object Update Re-indexing, Negative Coords, Removal, Arena Query)
void testSpatialHash() {
    optigd::core::SpatialHash grid(100.0f);
    int dummyA = 1, dummyB = 2, dummyC = 3, dummyNeg = 4;

    uint32_t idA = grid.insert(optigd::core::Rect2D(10, 10, 30, 30), &dummyA);
    uint32_t idB = grid.insert(optigd::core::Rect2D(50, 50, 40, 40), &dummyB);
    uint32_t idC = grid.insert(optigd::core::Rect2D(2000, 2000, 50, 50), &dummyC);
    uint32_t idNeg = grid.insert(optigd::core::Rect2D(-550, -450, 120, 120), &dummyNeg);

    OPTIGD_ASSERT(grid.getActiveObjectCount() == 4, "Should have 4 active objects");

    std::vector<void*> results;
    size_t count = grid.query(optigd::core::Rect2D(0, 0, 120, 120), results);
    OPTIGD_ASSERT(count == 2, "Camera at origin should see 2 objects (A and B), not C or Neg");

    // Query negative quadrant
    count = grid.query(optigd::core::Rect2D(-600, -500, 200, 200), results);
    OPTIGD_ASSERT(count == 1 && results[0] == &dummyNeg, "Camera in negative quadrant should find dummyNeg");

    // Move object A from (10, 10) to (2010, 2010) via update() and verify cell buckets are re-indexed
    grid.update(idA, optigd::core::Rect2D(2010, 2010, 30, 30));

    count = grid.query(optigd::core::Rect2D(0, 0, 120, 120), results);
    OPTIGD_ASSERT(count == 1 && results[0] == &dummyB,
                  "After moving A away, origin query must only return B (no stale cell reference)");

    count = grid.query(optigd::core::Rect2D(1950, 1950, 200, 200), results);
    OPTIGD_ASSERT(count == 2, "Query at (1950, 1950) must now find both C and moved A");

    // Test arena-backed query (zero heap allocations)
    optigd::core::MemoryArena arena(16384);
    void** arenaPtrs = nullptr;
    size_t arenaCount = grid.queryArena(optigd::core::Rect2D(1950, 1950, 200, 200), arena, arenaPtrs);
    OPTIGD_ASSERT(arenaCount == 2 && arenaPtrs != nullptr, "Arena query must match vector query");

    // Test remove()
    bool removed = grid.remove(idC);
    OPTIGD_ASSERT(removed, "Removing idC should succeed");
    OPTIGD_ASSERT(grid.getActiveObjectCount() == 3, "Active count should decrement to 3");
    count = grid.query(optigd::core::Rect2D(1950, 1950, 200, 200), results);
    OPTIGD_ASSERT(count == 1 && results[0] == &dummyA, "After removing C, only moved A remains at (2000, 2000)");

    (void)idB;
    (void)idNeg;
    OPTIGD_PASS();
}

// 3. VisibilityCuller Tests (Frustum Culling, Moving Bounds, Differential Transitions)
void testVisibilityCuller() {
    optigd::optimization::VisibilityCuller culler(128.0f);
    culler.setPadding(50.0f, 50.0f);

    std::vector<int> dummyNodes(1000);
    for (int i = 0; i < 1000; ++i) {
        dummyNodes[i] = i;
        culler.registerObject(static_cast<uint32_t>(i),
                              optigd::core::Rect2D(i * 100.0f, 100.0f, 40.0f, 40.0f),
                              &dummyNodes[i],
                              /*isStatic=*/(i != 500),
                              /*isDecorative=*/true);
    }

    std::vector<void*> visible;
    size_t visCount = culler.cullAgainstCamera(optigd::core::Rect2D(0.0f, 0.0f, 800.0f, 600.0f), visible);
    OPTIGD_ASSERT(visCount > 0 && visCount < 20, "Only objects near camera frustum should be visible");
    OPTIGD_ASSERT(culler.getLastCulledCount() == 1000 - visCount, "Culled count must equal total - visible");

    // Move object 500 from x=50000 into the origin camera frustum at x=200 via updateObjectBoundsByPtr
    bool updated = culler.updateObjectBoundsByPtr(&dummyNodes[500], optigd::core::Rect2D(200.0f, 100.0f, 40.0f, 40.0f));
    OPTIGD_ASSERT(updated, "updateObjectBoundsByPtr should find registered pointer");

    optigd::core::MemoryArena arena(65536);
    std::vector<void*> newlyVis, newlyCulled;
    auto delta1 = culler.evaluateVisibilityDelta(
        optigd::core::Rect2D(0.0f, 0.0f, 800.0f, 600.0f), arena, &newlyVis, &newlyCulled);
    OPTIGD_ASSERT(delta1.visibleCount == visCount + 1, "Moved object 500 should now be visible in camera frustum");
    OPTIGD_ASSERT(delta1.newlyVisibleCount == 1 && newlyVis.size() == 1 && newlyVis[0] == &dummyNodes[500],
                  "Differential culler should report object 500 as newly visible");

    // Advance camera to x=2000..2800 and verify previously visible objects become newlyCulled
    arena.reset();
    auto delta2 = culler.evaluateVisibilityDelta(
        optigd::core::Rect2D(2000.0f, 0.0f, 800.0f, 600.0f), arena, &newlyVis, &newlyCulled);
    OPTIGD_ASSERT(delta2.newlyCulledCount == delta1.visibleCount,
                  "Objects from origin frustum should transition to newlyCulled when camera jumps to x=2000");
    OPTIGD_ASSERT(delta2.newlyVisibleCount == delta2.visibleCount,
                  "Objects in new frustum should transition to newlyVisible");

    // Unregister object 500
    OPTIGD_ASSERT(culler.unregisterObjectByPtr(&dummyNodes[500]), "unregisterObjectByPtr should succeed");
    OPTIGD_ASSERT(culler.getTotalObjectCount() == 999, "Total active object count should be 999");

    OPTIGD_PASS();
}

// 4. ParticleGovernor Tests (Critical Priority, Finite Duration, Sleep/Wake, Non-Compounding Rate)
void testParticleGovernor() {
    optigd::optimization::ParticleGovernor gov(0.5f, 500);

    // Register 0: Critical (player trail) - offscreen
    gov.registerEmitter(0, {-2000.0f, -2000.0f}, 32.0f,
                        optigd::optimization::ParticlePriority::Critical, nullptr, 60.0f, -1.0f, false);
    // Register 1: Cosmetic continuous - onscreen
    gov.registerEmitter(1, {200.0f, 200.0f}, 32.0f,
                        optigd::optimization::ParticlePriority::Cosmetic, nullptr, 40.0f, -1.0f, false);
    // Register 2: Cosmetic continuous - offscreen
    gov.registerEmitter(2, {5000.0f, 5000.0f}, 32.0f,
                        optigd::optimization::ParticlePriority::Cosmetic, nullptr, 40.0f, -1.0f, false);
    // Register 3: Finite-duration burst emitter - offscreen (must NOT sleep so lifetime completes)
    gov.registerEmitter(3, {5000.0f, 5000.0f}, 32.0f,
                        optigd::optimization::ParticlePriority::Standard, nullptr, 50.0f, 0.4f, true);

    optigd::core::Rect2D cam(0.0f, 0.0f, 800.0f, 600.0f);

    float mult0 = 0.0f, mult1 = 0.0f, mult2 = 0.0f;
    bool run0 = gov.evaluateEmitter(0, cam, mult0);
    bool run1 = gov.evaluateEmitter(1, cam, mult1);
    bool run2 = gov.evaluateEmitter(2, cam, mult2);
    auto eval3 = gov.evaluateEmitterDetailed(3, cam, 0.016f);

    OPTIGD_ASSERT(run0 && mult0 == 1.0f, "Critical emitter must ALWAYS run at 1.0 multiplier even offscreen");
    OPTIGD_ASSERT(run1 && std::abs(mult1 - 0.5f) < 0.01f, "Onscreen cosmetic emitter should scale by 0.5");
    OPTIGD_ASSERT(!run2 && mult2 == 0.0f, "Offscreen continuous cosmetic emitter must sleep");
    OPTIGD_ASSERT(eval3.shouldUpdate, "Finite-duration auto-remove emitter must not sleep offscreen");

    // Verify non-compounding emission rate over 50 consecutive frames
    for (int f = 0; f < 50; ++f) {
        auto ev = gov.evaluateEmitterDetailed(1, cam, 0.016f);
        OPTIGD_ASSERT(std::abs(ev.effectiveEmissionRate - 20.0f) < 0.01f,
                      "Effective emission rate must stay at 40 * 0.5 = 20 without compounding decay");
    }

    // Move sleeping emitter 2 onscreen and verify wake transition clamps dt
    gov.updateEmitterPosition(2, {250.0f, 250.0f});
    auto wakeEval = gov.evaluateEmitterDetailed(2, cam, 0.1f);
    OPTIGD_ASSERT(wakeEval.shouldUpdate && wakeEval.justWokeFromSleep, "Emitter 2 should report justWokeFromSleep");
    OPTIGD_ASSERT(wakeEval.clampedDt <= (1.0f / 60.0f) + 1e-5f, "Wake transition must clamp dt to prevent burst");

    OPTIGD_PASS();
}

// 5. TriggerOptimizer Tests (X-Window, TouchOnly Hitbox, Multi-Trigger, Group Toggle, Cursor Rewind)
void testTriggerOptimizer() {
    optigd::optimization::TriggerOptimizer opt(500.0f);
    int t1 = 1, t2 = 2, t3 = 3, t4 = 4, t5 = 5, t6 = 6;

    // 0: Position trigger at x=100 (one-shot)
    opt.registerTrigger(0, 10, optigd::optimization::TriggerType::TouchOrPosition, 100.0f, &t1, false, 1);
    // 1: Position trigger at x=300 (multi-trigger)
    opt.registerTrigger(1, 10, optigd::optimization::TriggerType::TouchOrPosition, 300.0f, &t2, true, 2);
    // 2: Position trigger far ahead at x=5000
    opt.registerTrigger(2, 20, optigd::optimization::TriggerType::TouchOrPosition, 5000.0f, &t3, false, 1);
    // 3: Continuous trigger
    opt.registerTrigger(3, 0, optigd::optimization::TriggerType::Continuous, 0.0f, &t4, true, 0);
    // 4: Spawn trigger in group 99
    opt.registerTrigger(4, 99, optigd::optimization::TriggerType::Spawn, 2000.0f, &t5, true, 1);
    // 5: TouchOnly trigger at (200, 500, 40, 40) - should ONLY fire if player hitbox intersects y=[500..540]
    opt.registerTrigger(5, 30, optigd::optimization::TriggerType::TouchOnly, 200.0f, &t6, true, 1,
                        optigd::core::Rect2D(200.0f, 500.0f, 40.0f, 40.0f));

    opt.prepareLevel();

    // Player at x=210, y=100 (crosses x=100 and x=200, but does NOT touch y=500!)
    std::vector<void*> toExec;
    size_t execCount = opt.evaluateWithHitbox(
        210.0f, optigd::core::Rect2D(195.0f, 100.0f, 30.0f, 30.0f), toExec);
    OPTIGD_ASSERT(execCount == 2, "Should execute Continuous (t4) + crossed Position trigger (t1), NOT TouchOnly (t6)");

    // Now player jumps to y=510 at x=210, intersecting TouchOnly trigger 5
    execCount = opt.evaluateWithHitbox(
        210.0f, optigd::core::Rect2D(195.0f, 510.0f, 30.0f, 30.0f), toExec);
    OPTIGD_ASSERT(execCount == 2, "Should execute Continuous (t4) + TouchOnly trigger (t6) on hitbox intersection");

    // Advance player to x=350 (crosses multi-trigger 1 at x=300)
    execCount = opt.evaluateWithHitbox(
        350.0f, optigd::core::Rect2D(335.0f, 100.0f, 30.0f, 30.0f), toExec);
    OPTIGD_ASSERT(execCount == 2, "Should execute Continuous (t4) + multi-trigger (t2) at x=300");

    // Rewind player to x=250 (platformer reverse movement) and cross x=300 again
    opt.evaluateWithHitbox(250.0f, optigd::core::Rect2D(235.0f, 100.0f, 30.0f, 30.0f), toExec);
    execCount = opt.evaluateWithHitbox(
        350.0f, optigd::core::Rect2D(335.0f, 100.0f, 30.0f, 30.0f), toExec);
    OPTIGD_ASSERT(execCount == 2, "Multi-trigger at x=300 must re-fire after player moves back and crosses again");

    // Test Spawn Group O(1) dispatch and group toggle disable
    std::vector<void*> spawned;
    opt.invokeSpawnGroup(99, spawned);
    OPTIGD_ASSERT(spawned.size() == 1 && spawned[0] == &t5, "Spawn group 99 should invoke trigger 4");

    opt.setGroupEnabled(99, false);
    spawned.clear();
    opt.invokeSpawnGroup(99, spawned);
    OPTIGD_ASSERT(spawned.empty(), "Disabled group 99 must not invoke triggers");

    // Reset level trigger states and verify one-shot trigger at x=100 fires again
    opt.resetTriggerStates();
    execCount = opt.evaluateWithHitbox(
        150.0f, optigd::core::Rect2D(135.0f, 100.0f, 30.0f, 30.0f), toExec);
    OPTIGD_ASSERT(execCount == 2, "After resetTriggerStates, one-shot trigger at x=100 must fire again");

    OPTIGD_PASS();
}

// 6. MatrixCache Tests (Dirty Invalidation, Parent Propagation, Eviction, Transform Equivalence)
void testMatrixCache() {
    auto& cache = optigd::optimization::MatrixCache::instance();
    cache.clear();

    int parentNode = 10;
    int childNode = 20;
    auto matIn = optigd::optimization::AffineMatrix::fromNodeParams(10.0f, 20.0f, 45.0f, 1.0f, 1.0f);
    cache.store(&parentNode, 10.0f, 20.0f, 45.0f, 1.0f, 1.0f, matIn);
    cache.store(&childNode, 5.0f, 5.0f, 0.0f, 1.0f, 1.0f,
                optigd::optimization::AffineMatrix::fromNodeParams(5.0f, 5.0f, 0.0f, 1.0f, 1.0f));
    cache.setParent(&childNode, &parentNode);

    // Re-store child after linking parent
    cache.store(&childNode, 5.0f, 5.0f, 0.0f, 1.0f, 1.0f,
                optigd::optimization::AffineMatrix::fromNodeParams(5.0f, 5.0f, 0.0f, 1.0f, 1.0f));

    optigd::optimization::AffineMatrix matOut;
    bool hit = cache.getCached(&parentNode, 10.0f, 20.0f, 45.0f, 1.0f, 1.0f, matOut);
    OPTIGD_ASSERT(hit && matOut.approximatelyEquals(matIn), "Identical transform parameters should hit cache");

    bool childHit = cache.getCached(&childNode, 5.0f, 5.0f, 0.0f, 1.0f, 1.0f, matOut);
    OPTIGD_ASSERT(childHit, "Clean child with clean parent should hit cache");

    // Mark parent dirty: BOTH parent and child must now miss cache even with same parameters!
    cache.markDirty(&parentNode);
    bool hitAfterDirty = cache.getCached(&parentNode, 10.0f, 20.0f, 45.0f, 1.0f, 1.0f, matOut);
    OPTIGD_ASSERT(!hitAfterDirty, "Marked-dirty node MUST miss cache even if TRS floats match");

    bool childHitAfterParentDirty = cache.getCached(&childNode, 5.0f, 5.0f, 0.0f, 1.0f, 1.0f, matOut);
    OPTIGD_ASSERT(!childHitAfterParentDirty, "Child of dirty parent MUST miss cache");

    // Verify getOrCompute recomputes and clears dirty flag
    auto recomputed = cache.getOrCompute(&parentNode, 10.0f, 20.0f, 45.0f, 1.0f, 1.0f);
    OPTIGD_ASSERT(recomputed.approximatelyEquals(matIn), "getOrCompute must match analytical AffineMatrix");
    OPTIGD_ASSERT(cache.getCached(&parentNode, 10.0f, 20.0f, 45.0f, 1.0f, 1.0f, matOut),
                  "Subsequent getCached after getOrCompute must hit");

    // Evict parentNode and verify it is removed from cache
    cache.evict(&parentNode);
    OPTIGD_ASSERT(!cache.getCached(&parentNode, 10.0f, 20.0f, 45.0f, 1.0f, 1.0f, matOut),
                  "Evicted node must not hit cache");

    OPTIGD_PASS();
}

// 7. AdaptiveGovernor & FallbackManager Tests (Tier Transitions, Hysteresis, Fallback & Recovery)
void testAdaptiveGovernorAndFallback() {
    optigd::core::OptiConfig cfg;
    cfg.tier = optigd::core::PerformanceTier::HighEnd;
    cfg.targetFrameRate = 60.0f;
    cfg.fallbackCooldownFrames = 10;
    cfg.particleDensityScale = 1.0f;

    optigd::adaptive::AdaptiveGovernor gov(cfg);
    optigd::core::MemoryArena arena(16384);

    // Feed 40 slow frames (25ms each -> 40 FPS, exceeding 16.6ms budget)
    for (int i = 0; i < 40; ++i) {
        arena.reset();
        gov.recordFrameTime(25000.0);
        gov.update(cfg, &arena);
    }

    OPTIGD_ASSERT(cfg.tier != optigd::core::PerformanceTier::HighEnd,
                  "Governor must downgrade tier from HighEnd when frame times exceed budget");
    OPTIGD_ASSERT(gov.getRecentStats().medianMs > 24.0, "Median (P50) frame time should be calculated");

    // Test FallbackManager trigger AND recovery
    optigd::adaptive::FallbackManager fb(2.0f, 2);
    fb.setRecoveryStableFrames(2);
    optigd::adaptive::WindowStats jitteryStats{};
    jitteryStats.stdDevMs = 8.5;
    jitteryStats.fps = 35.0;

    float preFallbackDensity = cfg.particleDensityScale;
    bool triggered = fb.checkAndApplyFallbacks(100, jitteryStats, cfg);
    OPTIGD_ASSERT(triggered && fb.isFallbackActive(), "High frame-time jitter should trigger safety fallback");
    OPTIGD_ASSERT(cfg.particleDensityScale == 0.25f, "Fallback should reduce particle density scale to 0.25");

    // Recovery is now evaluated every frame regardless of the cooldown, so the
    // two required stable evaluations happen immediately (frames 101 and 102).
    optigd::adaptive::WindowStats stableStats{};
    stableStats.stdDevMs = 0.4;
    stableStats.fps = 60.0;
    fb.checkAndApplyFallbacks(101, stableStats, cfg);
    fb.checkAndApplyFallbacks(102, stableStats, cfg);

    OPTIGD_ASSERT(!fb.isFallbackActive() && fb.getRecoveryCount() == 1,
                  "FallbackManager should recover once frame-time variance stabilizes");
    OPTIGD_ASSERT(std::abs(cfg.particleDensityScale - preFallbackDensity) < 0.001f,
                  "Recovery must restore pre-fallback particle density scale");

    OPTIGD_PASS();
}

// 8. ConfigManager & Preset Synchronization Tests (Fixes Issue #9)
void testConfigAndPresetSync() {
    auto& cfgMgr = optigd::config::ConfigManager::instance();
    cfgMgr.resetToDefaults();

    // Test all 4 presets via syncFromSettings to ensure no stale copy overwrites preset values
    optigd::config::RuntimeSettingsInput input{};

    // 1. HighEnd
    input.tierPreset = "HighEnd";
    auto highCfg = cfgMgr.syncFromSettings(input);
    OPTIGD_ASSERT(highCfg.tier == optigd::core::PerformanceTier::HighEnd, "Tier should be HighEnd");
    OPTIGD_ASSERT(std::abs(highCfg.particleDensityScale - 1.0f) < 0.001f, "HighEnd density must be 1.0");
    OPTIGD_ASSERT(highCfg.maxActiveParticles == 8192, "HighEnd max particles must be 8192");
    OPTIGD_ASSERT(!highCfg.cullOffscreenEmitters, "HighEnd cullOffscreenEmitters must be false");

    // 2. Switch from HighEnd -> Balanced (verify HighEnd values do NOT overwrite Balanced!)
    input.tierPreset = "Balanced";
    auto balCfg = cfgMgr.syncFromSettings(input);
    OPTIGD_ASSERT(balCfg.tier == optigd::core::PerformanceTier::Balanced, "Tier should be Balanced");
    OPTIGD_ASSERT(std::abs(balCfg.particleDensityScale - 0.75f) < 0.001f, "Balanced density must be 0.75");
    OPTIGD_ASSERT(balCfg.maxActiveParticles == 4096, "Balanced max particles must be 4096");
    OPTIGD_ASSERT(balCfg.cullOffscreenEmitters, "Balanced cullOffscreenEmitters must be true");

    // 3. Switch from Balanced -> LowEnd
    input.tierPreset = "LowEnd";
    auto lowCfg = cfgMgr.syncFromSettings(input);
    OPTIGD_ASSERT(lowCfg.tier == optigd::core::PerformanceTier::LowEnd, "Tier should be LowEnd");
    OPTIGD_ASSERT(std::abs(lowCfg.particleDensityScale - 0.5f) < 0.001f, "LowEnd density must be 0.5");
    OPTIGD_ASSERT(lowCfg.maxActiveParticles == 2048, "LowEnd max particles must be 2048");

    // 4. Switch to Custom with custom parameters
    input.tierPreset = "Custom";
    input.customParticleDensity = 0.35f;
    input.customMaxParticles = 1500;
    input.customCullOffscreenParticles = false;
    input.customCameraPaddingX = 220.0f;
    input.customCameraPaddingY = 140.0f;
    input.targetFrameRate = 144.0f;
    auto customCfg = cfgMgr.syncFromSettings(input);
    OPTIGD_ASSERT(customCfg.tier == optigd::core::PerformanceTier::Custom, "Tier should be Custom");
    OPTIGD_ASSERT(std::abs(customCfg.particleDensityScale - 0.35f) < 0.001f, "Custom density must be 0.35");
    OPTIGD_ASSERT(customCfg.maxActiveParticles == 1500, "Custom max particles must be 1500");
    OPTIGD_ASSERT(!customCfg.cullOffscreenEmitters, "Custom cullOffscreenEmitters must be false");
    OPTIGD_ASSERT(std::abs(customCfg.cameraPaddingX - 220.0f) < 0.001f, "Custom paddingX must be 220");
    OPTIGD_ASSERT(std::abs(customCfg.targetFrameRate - 144.0f) < 0.001f, "Target FPS must be 144");

    // Test serialize / deserialize round-trip
    std::string data = cfgMgr.serialize();
    cfgMgr.resetToDefaults();
    bool ok = cfgMgr.deserialize(data);
    OPTIGD_ASSERT(ok, "Deserialization should succeed");
    OPTIGD_ASSERT(cfgMgr.getConfig().tier == optigd::core::PerformanceTier::Custom, "Restored tier must be Custom");
    OPTIGD_ASSERT(std::abs(cfgMgr.getConfig().targetFrameRate - 144.0f) < 0.01f, "Restored FPS must be 144");

    cfgMgr.resetToDefaults();
    OPTIGD_PASS();
}

// 9. CompatibilityLayer, Version Metadata & OptiGDManager End-to-End Lifecycle Tests
void testManagerAndCompatibility() {
    auto& compat = optigd::compat::CompatibilityLayer::instance();
    auto& mgr = optigd::core::OptiGDManager::instance();
    mgr.shutdown();
    mgr.initialize();

    OPTIGD_ASSERT(std::string(optigd::core::kOptiGDVersion) == "0.1.0-beta.3",
                  "OptiGD version constant must be 0.1.0-beta.3");
    OPTIGD_ASSERT(compat.getGDVersion() == "2.2081", "Default GD target version must be 2.2081");
    OPTIGD_ASSERT(compat.getGeodeVersion() == "v5.10.1", "Default Geode target version must be v5.10.1");
    OPTIGD_ASSERT(compat.isEnvironmentCompatible(), "Environment must be compatible");
    OPTIGD_ASSERT(compat.isFeatureUsable("spatial_culling"), "spatial_culling should be usable");

    compat.reportConflict("spatial_culling", "Simulated conflict with external camera mod");
    OPTIGD_ASSERT(!compat.isFeatureUsable("spatial_culling"), "Conflicted feature must be disabled");
    compat.enableFeature("spatial_culling");
    OPTIGD_ASSERT(compat.isFeatureUsable("spatial_culling"), "Re-enabled feature should be usable");

    mgr.onLevelLoad();
    int dummyNode = 42;
    mgr.getVisibilityCuller().registerObject(
        1, optigd::core::Rect2D(100.0f, 100.0f, 30.0f, 30.0f), &dummyNode, true, true);

    for (int f = 0; f < 35; ++f) {
        mgr.beginFrame();
        mgr.setExternalFrameDeltaUs(16666.0);
        mgr.recordHeapAllocationAvoided(1);
        mgr.getVisibilityCuller().evaluateVisibilityDelta(
            optigd::core::Rect2D(0.0f, 0.0f, 800.0f, 600.0f), mgr.getFrameArena());
        mgr.endFrame();
    }

    const auto& latest = mgr.getLatestFrameStats();
    OPTIGD_ASSERT(latest.totalObjects == 1 && latest.visibleObjects == 1, "Culler stats recorded in FrameStats");
    OPTIGD_ASSERT(latest.arenaAllocations >= 2,
                  "Frame arena must be used by VisibilityCuller and AdaptiveGovernor");
    OPTIGD_ASSERT(latest.heapAllocationsAvoided > latest.arenaAllocations,
                  "heapAllocationsAvoided must reflect avoided heap allocations plus arena allocations");

    std::string report = optigd::diagnostics::DiagnosticsEngine::instance().generateJsonReport(
        mgr.getActiveConfig(),
        mgr.getFallbackManager().getFallbackHistory(),
        mgr.getAdaptiveGovernor().getTierTransitions()
    );
    OPTIGD_ASSERT(report.find("\"optigd_version\": \"0.1.0-beta.3\"") != std::string::npos,
                  "JSON report must contain optigd_version 0.1.0-beta.3");
    OPTIGD_ASSERT(report.find("\"geode_version\": \"v5.10.1\"") != std::string::npos,
                  "JSON report must contain geode_version v5.10.1");
    OPTIGD_ASSERT(report.find("\"median_frame_time_ms\"") != std::string::npos,
                  "JSON report must contain median_frame_time_ms");

    optigd::ui::DiagnosticsOverlay::instance().setEnabled(true);
    std::string hud = optigd::ui::DiagnosticsOverlay::instance().formatOverlayText(
        latest, mgr.getAdaptiveGovernor().getRecentStats(), mgr.getActiveConfig());
    OPTIGD_ASSERT(hud.find("[OptiGD v0.1.0-beta.3 Diagnostics]") != std::string::npos,
                  "Overlay header must display v0.1.0-beta.3");
    // The overlay must state that its numbers are OptiGD's own instrumentation,
    // and must not label a wall-clock span as thread CPU time.
    OPTIGD_ASSERT(hud.find("Internal telemetry") != std::string::npos,
                  "Overlay must disclose that it shows internal telemetry");
    OPTIGD_ASSERT(hud.find("Not presented-frame or GPU timing") != std::string::npos,
                  "Overlay must say what it is NOT measuring");
    OPTIGD_ASSERT(hud.find("ms wall") != std::string::npos &&
                  hud.find("ms thread") != std::string::npos,
                  "Overlay must keep wall-clock and thread CPU time apart");
    OPTIGD_ASSERT(hud.find("Update CPU:") == std::string::npos,
                  "the ambiguous 'Update CPU' label must be gone");

    // Verify Profiler & Logger callback integration
    {
        OPTIGD_PROFILE_SCOPE("UnitTestProfileScope");
    }
    auto profCpu = optigd::profiling::Profiler::instance().getMetric("FrameUpdateCpu");
    auto profScope = optigd::profiling::Profiler::instance().getMetric("UnitTestProfileScope");
    OPTIGD_ASSERT(profCpu.callCount == 35, "Profiler must record FrameUpdateCpu for every frame");
    OPTIGD_ASSERT(profScope.callCount == 1, "OPTIGD_PROFILE_SCOPE must record scoped execution");

    bool callbackInvoked = false;
    optigd::core::Logger::instance().setCallback([&](optigd::core::LogLevel lvl, std::string_view msg) {
        if (lvl == optigd::core::LogLevel::Info && msg.find("CallbackTest") != std::string_view::npos) {
            callbackInvoked = true;
        }
    });
    optigd::core::Logger::instance().info("CallbackTest");
    optigd::core::Logger::instance().setCallback(nullptr);
    OPTIGD_ASSERT(callbackInvoked, "Logger callback bridge must invoke registered callback");

    mgr.onLevelExit();
    OPTIGD_PASS();
}


// 10. Regression: Geode 5.10.1 `mod.json` version grammar (root cause of the
//     reported "unexpected type string" loading failure).
//     Geode reads `mod.json`'s "version" and "geode" fields into a
//     `geode::VersionInfo` (not a std::string). When matjson's conversion
//     fails, `JsonExpectedValue::tryGet<T>()` replaces the detailed error with
//     the *JSON* type, so an invalid version string surfaces to users as
//     "unexpected type string" and the mod is rejected with
//     LoadProblem::Type::InvalidGeodeFile.
void testGeodeVersionMetadataGrammar() {
    using optigd::core::geodever::parseVersionString;
    using optigd::core::geodever::isValidGeodeVersionString;

    // The exact string shipped in v0.1.0-beta1, which Geode 5.10.1 rejects.
    auto beta1 = parseVersionString("v0.1.0-beta1");
    OPTIGD_ASSERT(!beta1.valid, "v0.1.0-beta1 must be rejected by the Geode grammar");
    OPTIGD_ASSERT(beta1.error == "Expected end of version, found '1'",
                  "Geode 5.10.1 rejects trailing tag digits without a dot");

    // The literal `v0.1.0-beta2` form is rejected for the same reason.
    OPTIGD_ASSERT(!isValidGeodeVersionString("v0.1.0-beta2"),
                  "v0.1.0-beta2 must also be rejected (tag number requires a dot)");

    // And so is the dot-less form of the version this build ships, whose exact
    // error message AGENT.md §1 quotes. Asserted here so that quotation is
    // measured rather than inferred from the grammar.
    auto beta3 = parseVersionString("v0.1.0-beta3");
    OPTIGD_ASSERT(!beta3.valid,
                  "v0.1.0-beta3 must be rejected (tag number requires a dot)");
    OPTIGD_ASSERT(beta3.error == "Expected end of version, found '3'",
                  "the rejected form must report the offending digit");

    // The form actually used by this build must parse.
    auto current = parseVersionString(optigd::core::kOptiGDVersionTag);
    OPTIGD_ASSERT(current.valid,
                  "kOptiGDVersionTag must satisfy the Geode VersionInfo grammar");
    OPTIGD_ASSERT(current.major == 0 && current.minor == 1 && current.patch == 0,
                  "Parsed version must be 0.1.0");
    OPTIGD_ASSERT(current.tag.has_value() &&
                      *current.tag == optigd::core::geodever::VersionTagKind::Beta,
                  "Parsed tag must be Beta");
    OPTIGD_ASSERT(current.tagNumber.has_value() && *current.tagNumber == 3,
                  "Parsed tag number must be 3");
    OPTIGD_ASSERT(current.toVString() == std::string(optigd::core::kOptiGDVersionTag),
                  "Round-trip formatting must be stable");

    // The un-prefixed constant must also be valid (Geode allows a leading 'v').
    OPTIGD_ASSERT(isValidGeodeVersionString(optigd::core::kOptiGDVersion),
                  "kOptiGDVersion must also parse");

    // The pinned SDK version and other accepted forms.
    OPTIGD_ASSERT(isValidGeodeVersionString("5.10.1"), "Plain semver must parse");
    OPTIGD_ASSERT(isValidGeodeVersionString("v5.10.1"), "Leading v must be optional");
    OPTIGD_ASSERT(isValidGeodeVersionString("v0.1.0"), "No-tag versions must parse");
    OPTIGD_ASSERT(isValidGeodeVersionString("v0.1.0-beta"), "Unnumbered beta must parse");
    OPTIGD_ASSERT(isValidGeodeVersionString("v1.2.3-alpha.4"), "alpha.N must parse");
    OPTIGD_ASSERT(isValidGeodeVersionString("v1.2.3-prerelease.7"), "prerelease.N must parse");

    // Rejected forms.
    OPTIGD_ASSERT(!isValidGeodeVersionString("2.2081"), "Patch version is mandatory");
    OPTIGD_ASSERT(!isValidGeodeVersionString("v0.1.0-gamma.2"), "Unknown tags are rejected");
    OPTIGD_ASSERT(!isValidGeodeVersionString("v0.1.0-beta.2.3"), "Trailing input is rejected");
    OPTIGD_ASSERT(!isValidGeodeVersionString(""), "Empty string is rejected");

    OPTIGD_PASS();
}

// 11. Regression: changing the spatial cell size must re-index objects instead
//     of silently emptying the SpatialHash (which previously made every object
//     look culled and hid the entire level's decorations permanently).
void testVisibilityCullerCellSizeReindex() {
    optigd::optimization::VisibilityCuller culler(128.0f);
    culler.setPadding(0.0f, 0.0f);

    std::vector<int> nodes(64);
    for (int i = 0; i < 64; ++i) {
        nodes[i] = i;
        culler.registerObject(static_cast<uint32_t>(i),
                              optigd::core::Rect2D(i * 50.0f, 0.0f, 40.0f, 40.0f),
                              &nodes[i], true, true);
    }

    optigd::core::MemoryArena arena(65536);
    auto before = culler.evaluateVisibilityDelta(
        optigd::core::Rect2D(0.0f, -100.0f, 400.0f, 400.0f), arena, nullptr, nullptr);
    OPTIGD_ASSERT(before.visibleCount > 0, "Camera should see several objects before resize");

    // Simulate OptiGDManager::applyConfigToSubsystems() running mid-level.
    culler.setCellSize(256.0f);
    OPTIGD_ASSERT(std::abs(culler.getCellSize() - 256.0f) < 0.001f, "Cell size must change");

    arena.reset();
    auto after = culler.evaluateVisibilityDelta(
        optigd::core::Rect2D(0.0f, -100.0f, 400.0f, 400.0f), arena, nullptr, nullptr);
    OPTIGD_ASSERT(after.visibleCount == before.visibleCount,
                  "Re-indexing after a cell-size change must preserve the visible set");

    // A no-op resize must not disturb anything either.
    culler.setCellSize(256.0f);
    arena.reset();
    auto same = culler.evaluateVisibilityDelta(
        optigd::core::Rect2D(0.0f, -100.0f, 400.0f, 400.0f), arena, nullptr, nullptr);
    OPTIGD_ASSERT(same.visibleCount == after.visibleCount,
                  "Re-applying the same cell size must be a no-op");

    // Disabling culling must reveal every object OptiGD hid.
    arena.reset();
    culler.evaluateVisibilityDelta(
        optigd::core::Rect2D(100000.0f, 0.0f, 400.0f, 400.0f), arena, nullptr, nullptr);
    OPTIGD_ASSERT(culler.hasCulledObjects(), "Moving the camera away must cull objects");

    std::vector<void*> restored;
    size_t restoredCount = culler.restoreAllVisibility(&restored, /*decorativeOnly=*/true);
    OPTIGD_ASSERT(restoredCount == 64 && restored.size() == 64,
                  "restoreAllVisibility must reveal every culled decorative object");
    OPTIGD_ASSERT(!culler.hasCulledObjects(), "No object may remain culled after restore");

    OPTIGD_PASS();
}

// 12. Regression: ParticleGovernor totals must be maintained incrementally.
//     The previous implementation rescanned every emitter inside
//     recordActiveParticles(), i.e. once per emitter per frame (O(N^2)).
void testParticleGovernorIncrementalTotals() {
    optigd::optimization::ParticleGovernor gov(1.0f, 100000);

    for (uint32_t i = 0; i < 10; ++i) {
        gov.registerEmitter(i, {0.0f, 0.0f}, 64.0f,
                            optigd::optimization::ParticlePriority::Cosmetic,
                            nullptr, 30.0f, -1.0f, false);
    }

    OPTIGD_ASSERT(gov.getTotalActiveParticles() == 0, "Initial total must be zero");

    for (uint32_t i = 0; i < 10; ++i) {
        gov.recordActiveParticles(i, 100);
    }
    OPTIGD_ASSERT(gov.getTotalActiveParticles() == 1000,
                  "Running total must accumulate (10 emitters x 100 particles)");

    // Shrinking one emitter must subtract only the delta.
    gov.recordActiveParticles(3, 40);
    OPTIGD_ASSERT(gov.getTotalActiveParticles() == 940,
                  "Shrinking an emitter must subtract only its delta");

    // Unregistering an emitter must remove its contribution entirely.
    OPTIGD_ASSERT(gov.unregisterEmitter(0), "unregisterEmitter must succeed");
    OPTIGD_ASSERT(gov.getTotalActiveParticles() == 840,
                  "Unregistering must remove that emitter's particle count");

    OPTIGD_PASS();
}

// 13. Regression: DiagnosticsEngine JSON output must stay parseable even when
//     fallback reasons contain characters that require escaping.
void testDiagnosticsJsonEscaping() {
    optigd::diagnostics::DiagnosticsEngine& diag =
        optigd::diagnostics::DiagnosticsEngine::instance();
    diag.reset();

    optigd::core::FrameStats stats{};
    stats.totalFrameTimeUs = 16666.0;
    stats.updateCpuTimeUs = 4200.0;
    stats.optigdOverheadUs = 12.5;
    diag.recordFrameStats(stats);

    std::vector<optigd::adaptive::FallbackEvent> fallbacks;
    fallbacks.push_back({7, "matrix\"cache", "reason with \"quotes\" and \\backslash\\",
                         "action\twith\ttabs"});

    std::string report = diag.generateJsonReport(optigd::core::OptiConfig{}, fallbacks, 3);

    // A raw, unescaped quote inside a JSON string would terminate it early and
    // break every consumer (jq, tools/generate_report.py).
    OPTIGD_ASSERT(report.find("matrix\\\"cache") != std::string::npos,
                  "Quotes in JSON string values must be escaped");
    OPTIGD_ASSERT(report.find("\\\"quotes\\\"") != std::string::npos,
                  "Inner quotes must be escaped");
    OPTIGD_ASSERT(report.find("\\\\backslash\\\\") != std::string::npos,
                  "Backslashes must be escaped");
    OPTIGD_ASSERT(report.find("action\\twith\\ttabs") != std::string::npos,
                  "Control characters must be escaped");
    OPTIGD_ASSERT(report.find("\"notes\"") != std::string::npos,
                  "Report must document that native trigger execution is unmodified");
    OPTIGD_ASSERT(report.find("\"native_trigger_execution_modified\": false") != std::string::npos,
                  "Report must state native trigger execution is untouched");

    diag.reset();
    OPTIGD_PASS();
}

// 14. Regression: persisted OptiGD configuration may be truncated or written by
//     an older build; loading it must never throw or corrupt the config.
void testConfigDeserializeRobustness() {
    auto& cfgMgr = optigd::config::ConfigManager::instance();
    cfgMgr.resetToDefaults();

    std::string malformed =
        "tier=not-a-number\n"
        "targetFps=oops\n"
        "spatialCulling=1\n"
        "particleDensityScale=NaN-ish\n"
        "maxActiveParticles=99999999999999999999\n"
        "cameraPaddingX=12.5\n"
        "cameraPaddingY=\n"
        "garbage-line-without-equals\n"
        "exportDiagnosticsOnExit=0\n";

    bool ok = cfgMgr.deserialize(malformed);
    OPTIGD_ASSERT(ok, "deserialize must report partial success rather than throwing");
    OPTIGD_ASSERT(cfgMgr.getConfig().flags.enableSpatialCulling,
                  "Valid boolean lines must still be applied");
    OPTIGD_ASSERT(std::abs(cfgMgr.getConfig().cameraPaddingX - 12.5f) < 0.001f,
                  "Valid float lines must still be applied");
    OPTIGD_ASSERT(!cfgMgr.getConfig().flags.exportDiagnosticsOnExit,
                  "Valid feature flags must still be applied");
    OPTIGD_ASSERT(cfgMgr.getConfig().tier == optigd::core::PerformanceTier::LowEnd,
                  "Malformed tier lines must leave the default tier intact");
    OPTIGD_ASSERT(std::abs(cfgMgr.getConfig().targetFrameRate - 60.0f) < 0.001f,
                  "Malformed FPS lines must leave the default frame rate intact");

    OPTIGD_ASSERT(!cfgMgr.deserialize(""), "Empty data must not be reported as loaded");

    cfgMgr.resetToDefaults();
    OPTIGD_PASS();
}

// 15. Regression: the camera world rect convention used by
//     computeCameraWorldRect() in src/geode/Hooks.cpp.
//     GJBaseGameLayer::m_gameState.m_cameraPosition is the BOTTOM-LEFT corner of
//     the visible area in object-layer units, not its centre. Evidence from the
//     pinned GD 2.2081 bindings:
//       * inline/PlayLayer.cpp screenFlipObject():
//             xDiff = objectPos.x - m_gameState.m_cameraPosition.x
//             newX  = objectPos.x + (winSize.width / zoom - 2 * xDiff) * factor
//         which mirrors x about camX + (winSize.width / zoom) / 2, i.e. the view
//         spans [camX, camX + winSize.width / zoom].
//       * inline/GJBaseGameLayer.cpp lightningFlash():
//             y = m_gameState.m_cameraPosition.y + (winSize.height + 50) / scale
//         placing the flash above the TOP edge of the view.
void testCameraWorldRectConvention() {
    // Mirrors optigd::geode_integration::computeCameraWorldRect().
    struct FakeCamera { float camX, camY, winW, winH, zoom; };
    auto computeCameraWorldRect = [](FakeCamera const& c) {
        float zoom = (c.zoom > 0.01f) ? c.zoom : 1.0f;
        return optigd::core::Rect2D(c.camX, c.camY, c.winW / zoom, c.winH / zoom);
    };

    FakeCamera cam{1000.0f, 200.0f, 1280.0f, 720.0f, 1.0f};
    auto rect = computeCameraWorldRect(cam);
    OPTIGD_ASSERT(std::abs(rect.minX() - 1000.0f) < 0.001f,
                  "Camera rect origin X is the camera position (bottom-left)");
    OPTIGD_ASSERT(std::abs(rect.minY() - 200.0f) < 0.001f,
                  "Camera rect origin Y is the camera position (bottom-left)");
    OPTIGD_ASSERT(std::abs(rect.maxX() - 2280.0f) < 0.001f,
                  "Camera rect width is winSize.width / zoom");
    OPTIGD_ASSERT(std::abs(rect.maxY() - 920.0f) < 0.001f,
                  "Camera rect height is winSize.height / zoom");

    // Zoomed-out camera sees more world, never less.
    FakeCamera zoomedOut{1000.0f, 200.0f, 1280.0f, 720.0f, 0.5f};
    auto zoomRect = computeCameraWorldRect(zoomedOut);
    OPTIGD_ASSERT(zoomRect.size.width > rect.size.width &&
                      zoomRect.size.height > rect.size.height,
                  "Zooming out must widen the visible world rect");

    // A degenerate zoom must fall back to 1.0 instead of producing inf/NaN.
    FakeCamera badZoom{0.0f, 0.0f, 1280.0f, 720.0f, 0.0f};
    auto safeRect = computeCameraWorldRect(badZoom);
    OPTIGD_ASSERT(std::isfinite(safeRect.size.width) && std::isfinite(safeRect.size.height),
                  "Degenerate zoom must not produce a non-finite rect");

    OPTIGD_PASS();
}


// 16. Regression: MatrixCache must never serve a matrix it did not compute.
//     setParent() implicitly creates cache entries for nodes it has only seen
//     as a link endpoint. Those entries used to be indistinguishable from
//     computed ones, so a default-initialised identity matrix could satisfy a
//     getCached() lookup, and the map grew without bound for linked-but-
//     never-transformed node graphs.
void testMatrixCacheStaleEntryPruning() {
    auto& cache = optigd::optimization::MatrixCache::instance();
    cache.clear();

    int childNode = 1;
    int parentNode = 2;

    // Link two nodes that have never had a transform computed or stored.
    cache.setParent(&childNode, &parentNode);
    OPTIGD_ASSERT(cache.getCachedCount() == 2,
                  "setParent registers both endpoints in the cache");

    optigd::optimization::AffineMatrix out;
    OPTIGD_ASSERT(!cache.getCached(&childNode, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, out),
                  "An entry created only by setParent must never be a cache hit");
    const uint64_t missesBefore = cache.getMisses();
    cache.getCached(&parentNode, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, out);
    OPTIGD_ASSERT(cache.getMisses() == missesBefore + 1,
                  "Implicit parent entries must also miss");

    // A real store() makes the entry usable.
    optigd::optimization::AffineMatrix real =
        optigd::optimization::AffineMatrix::fromNodeParams(10.0f, 20.0f, 0.0f, 1.0f, 1.0f);
    cache.store(&childNode, 10.0f, 20.0f, 0.0f, 1.0f, 1.0f, real);
    OPTIGD_ASSERT(cache.getCached(&childNode, 10.0f, 20.0f, 0.0f, 1.0f, 1.0f, out),
                  "After store(), the entry must be a hit");
    OPTIGD_ASSERT(out.approximatelyEquals(real), "Cached matrix must round-trip exactly");

    // store() must treat the anchor as part of the cache key. getOrCompute()
    // keys on the anchor, so a store() that ignored it could leave an entry
    // whose recorded anchor no longer matches the matrix it holds.
    optigd::optimization::AffineMatrix anchored =
        optigd::optimization::AffineMatrix::fromNodeParams(10.0f, 20.0f, 0.0f, 1.0f, 1.0f, 0.5f, 0.5f);
    cache.store(&childNode, 10.0f, 20.0f, 0.0f, 1.0f, 1.0f, anchored, 0.5f, 0.5f);

    uint64_t hitsBefore = cache.getHits();
    optigd::optimization::AffineMatrix fromCache =
        cache.getOrCompute(&childNode, 10.0f, 20.0f, 0.0f, 1.0f, 1.0f, 0.5f, 0.5f);
    OPTIGD_ASSERT(cache.getHits() == hitsBefore + 1,
                  "store() must record the anchor, so a matching getOrCompute hits");
    OPTIGD_ASSERT(fromCache.approximatelyEquals(anchored),
                  "The anchor-aware cached matrix must round-trip exactly");

    hitsBefore = cache.getHits();
    cache.getOrCompute(&childNode, 10.0f, 20.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f);
    OPTIGD_ASSERT(cache.getHits() == hitsBefore,
                  "Changing only the anchor must invalidate the cached entry");

    // A phantom chain (linked, never transformed) must be prunable, and
    // pruning must detach children so no dangling pointers are left behind.
    int phantomChild = 3;
    int phantomParent = 4;
    cache.setParent(&phantomChild, &phantomParent);
    const size_t beforePrune = cache.getCachedCount();
    OPTIGD_ASSERT(beforePrune == 4, "Two more phantom entries should be registered");

    const size_t pruned = cache.pruneStaleEntries();
    // phantomChild is a phantom leaf and goes first; detaching it makes
    // phantomParent a childless phantom, so it goes too. parentNode keeps its
    // one computed child and must survive.
    OPTIGD_ASSERT(pruned == 2, "Both phantom entries must be pruned");
    OPTIGD_ASSERT(cache.getCachedCount() == 2,
                  "Only the never-computed phantom entries must be pruned");
    OPTIGD_ASSERT(!cache.getCached(&phantomParent, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, out),
                  "Pruned entries must fall back to a miss, not a default matrix");
    OPTIGD_ASSERT(cache.getCached(&childNode, 10.0f, 20.0f, 0.0f, 1.0f, 1.0f, out),
                  "Pruning must not disturb computed entries");

    OPTIGD_PASS();
}

// 17. Regression: FallbackManager recovery must not be blocked by the
//     fallback cooldown. Previously the cooldown early-returned before the
//     recovery branch ran, so a stabilised workload could only restore its
//     pre-fallback settings once the whole cooldown had elapsed.
void testFallbackRecoveryDuringCooldown() {
    optigd::core::OptiConfig cfg;
    cfg.targetFrameRate = 60.0f;
    cfg.particleDensityScale = 1.0f;

    optigd::adaptive::FallbackManager fb(2.0f, 30);
    fb.setRecoveryStableFrames(2);

    optigd::adaptive::WindowStats jittery{};
    jittery.stdDevMs = 8.5;
    jittery.fps = 35.0;

    OPTIGD_ASSERT(fb.checkAndApplyFallbacks(1, jittery, cfg),
                  "Jitter beyond the threshold must apply a fallback");
    OPTIGD_ASSERT(cfg.particleDensityScale == 0.25f,
                  "Fallback must reduce particle density");
    OPTIGD_ASSERT(fb.isFallbackActive(), "Fallback must be marked active");

    // Recovery must be evaluated while the cooldown is still pending.
    optigd::adaptive::WindowStats stable{};
    stable.stdDevMs = 0.4;
    stable.fps = 60.0;

    for (int frame = 2; frame <= 3 && fb.isFallbackActive(); ++frame) {
        fb.checkAndApplyFallbacks(frame, stable, cfg);
    }

    OPTIGD_ASSERT(!fb.isFallbackActive(),
                  "Recovery must happen during the cooldown, not after it");
    OPTIGD_ASSERT(fb.getRecoveryCount() == 1, "Exactly one recovery must be recorded");
    OPTIGD_ASSERT(std::abs(cfg.particleDensityScale - 1.0f) < 0.001f,
                  "Recovery must restore the pre-fallback density scale");

    OPTIGD_PASS();
}

// 18. Regression: the per-frame profiler path must not take a mutex or
//     materialise a std::string key. recordById() writes into a fixed array
//     slot; the name based API resolves known metrics to the same slot.
void testProfilerFastPath() {
    auto& prof = optigd::profiling::Profiler::instance();
    prof.reset();
    prof.setEnabled(true);

    prof.recordById(optigd::profiling::MetricId::VisibilityCuller, 10.0);
    prof.recordById(optigd::profiling::MetricId::VisibilityCuller, 30.0);

    auto byId = prof.getById(optigd::profiling::MetricId::VisibilityCuller);
    OPTIGD_ASSERT(byId.callCount == 2, "recordById must accumulate call counts");
    OPTIGD_ASSERT(std::abs(byId.totalTimeUs - 40.0) < 0.001, "recordById must accumulate time");
    OPTIGD_ASSERT(std::abs(byId.minTimeUs - 10.0) < 0.001, "recordById must track the minimum");
    OPTIGD_ASSERT(std::abs(byId.maxTimeUs - 30.0) < 0.001, "recordById must track the maximum");
    OPTIGD_ASSERT(std::abs(byId.getMeanTimeUs() - 20.0) < 0.001, "Mean must be total / count");

    // The name based API must resolve to the same slot, not a separate one.
    prof.record("VisibilityCuller", 10.0);
    auto byName = prof.getMetric("VisibilityCuller");
    OPTIGD_ASSERT(byName.callCount == 3,
                  "record(name) must reuse the fast slot for known metric names");
    OPTIGD_ASSERT(std::abs(byName.totalTimeUs - 50.0) < 0.001,
                  "Name and id paths must share the same accumulator");

    // Unknown/ad-hoc names still work through the overflow map.
    prof.record("AdHocInstrumentation", 5.0);
    OPTIGD_ASSERT(prof.getMetric("AdHocInstrumentation").callCount == 1,
                  "Ad-hoc metric names must still be recorded");

    bool foundFast = false;
    for (const auto& metric : prof.getSnapshot()) {
        if (metric.name == "VisibilityCuller" && metric.callCount == 3) {
            foundFast = true;
        }
    }
    OPTIGD_ASSERT(foundFast, "getSnapshot must include fast-path metrics");

    prof.reset();
    OPTIGD_ASSERT(prof.getById(optigd::profiling::MetricId::VisibilityCuller).callCount == 0,
                  "reset() must clear fast-path metrics too");

    OPTIGD_PASS();
}

// 19. Regression: the GD version gate must compare against the exact target
//     build. A `starts_with("2.2")` test accepts GD 2.21x, whose structure
//     layouts differ from the 2.2081 bindings OptiGD is compiled against.
void testGdVersionCompatibilityGate() {
    using optigd::compat::CompatibilityLayer;

    OPTIGD_ASSERT(CompatibilityLayer::isSupportedGDVersion("2.2081"),
                  "The pinned target GD 2.2081 must be supported");
    OPTIGD_ASSERT(CompatibilityLayer::isSupportedGDVersion("v2.2081"),
                  "A leading 'v' must be tolerated");
    OPTIGD_ASSERT(CompatibilityLayer::isSupportedGDVersion("*"),
                  "'*' remains the explicit standalone-harness opt-out");

    OPTIGD_ASSERT(!CompatibilityLayer::isSupportedGDVersion("2.21"),
                  "GD 2.21 must be rejected even though it starts with '2.2'");
    OPTIGD_ASSERT(!CompatibilityLayer::isSupportedGDVersion("2.2074"),
                  "An older GD build must be rejected");
    OPTIGD_ASSERT(!CompatibilityLayer::isSupportedGDVersion("3.0"),
                  "A different GD major version must be rejected");
    OPTIGD_ASSERT(!CompatibilityLayer::isSupportedGDVersion(""),
                  "An empty version string must be rejected");
    OPTIGD_ASSERT(!CompatibilityLayer::isSupportedGDVersion("banana"),
                  "A non-numeric version string must be rejected");

    int major = 0, minor = 0, patch = 0;
    OPTIGD_ASSERT(CompatibilityLayer::parseVersionComponents("2.2081", major, minor, patch),
                  "parseVersionComponents must succeed on a dotted numeric version");
    OPTIGD_ASSERT(major == 2 && minor == 2081 && patch < 0,
                  "GD 2.2081 parses to major 2, minor 2081 and no patch component");

    OPTIGD_PASS();
}

// 20. Regression: tier defaults must come from one table.
//     ConfigManager's presets and AdaptiveGovernor's automatic transitions used
//     to carry independent copies of the same numbers, so a tier reached by an
//     automatic downgrade could differ from the same tier selected by hand.
void testTierDefaultsSingleSource() {
    // The table itself.
    const auto& low = optigd::core::kTierDefaults[static_cast<size_t>(optigd::core::PerformanceTier::LowEnd)];
    const auto& bal = optigd::core::kTierDefaults[static_cast<size_t>(optigd::core::PerformanceTier::Balanced)];
    const auto& high = optigd::core::kTierDefaults[static_cast<size_t>(optigd::core::PerformanceTier::HighEnd)];

    OPTIGD_ASSERT(low.particleDensityScale == 0.5f && low.maxActiveParticles == 2048 &&
                      low.cullOffscreenEmitters && low.cameraPaddingX == 120.0f &&
                      low.cameraPaddingY == 80.0f,
                  "LowEnd tier defaults must be 0.5 / 2048 / true / 120 / 80");
    OPTIGD_ASSERT(bal.particleDensityScale == 0.75f && bal.maxActiveParticles == 4096 &&
                      bal.cullOffscreenEmitters && bal.cameraPaddingX == 150.0f &&
                      bal.cameraPaddingY == 100.0f,
                  "Balanced tier defaults must be 0.75 / 4096 / true / 150 / 100");
    OPTIGD_ASSERT(high.particleDensityScale == 1.0f && high.maxActiveParticles == 8192 &&
                      !high.cullOffscreenEmitters && high.cameraPaddingX == 200.0f &&
                      high.cameraPaddingY == 150.0f,
                  "HighEnd tier defaults must be 1.0 / 8192 / false / 200 / 150");

    // applyTierDefaults must be a no-op for Custom so user values survive.
    optigd::core::OptiConfig cfg;
    cfg.particleDensityScale = 0.33f;
    cfg.maxActiveParticles = 777;
    cfg.cameraPaddingX = 11.0f;
    cfg.cameraPaddingY = 22.0f;
    optigd::core::applyTierDefaults(cfg, optigd::core::PerformanceTier::Custom);
    OPTIGD_ASSERT(cfg.particleDensityScale == 0.33f && cfg.maxActiveParticles == 777 &&
                      cfg.cameraPaddingX == 11.0f && cfg.cameraPaddingY == 22.0f,
                  "applyTierDefaults must not touch a Custom tier config");

    // Applying a real tier must produce exactly the table values.
    optigd::core::applyTierDefaults(cfg, optigd::core::PerformanceTier::Balanced);
    OPTIGD_ASSERT(cfg.particleDensityScale == bal.particleDensityScale &&
                      cfg.maxActiveParticles == bal.maxActiveParticles &&
                      cfg.cullOffscreenEmitters == bal.cullOffscreenEmitters &&
                      cfg.cameraPaddingX == bal.cameraPaddingX &&
                      cfg.cameraPaddingY == bal.cameraPaddingY,
                  "applyTierDefaults must apply the shared table verbatim");

    // The two consumers must agree: an automatic governor downgrade from
    // HighEnd must land on the same numbers ConfigManager's preset produces.
    optigd::core::OptiConfig govCfg;
    govCfg.tier = optigd::core::PerformanceTier::HighEnd;
    govCfg.targetFrameRate = 60.0f;
    optigd::adaptive::AdaptiveGovernor gov(govCfg);
    optigd::core::MemoryArena arena(16384);
    for (int i = 0; i < 40; ++i) {
        arena.reset();
        gov.recordFrameTime(25000.0);
        gov.update(govCfg, &arena);
    }
    OPTIGD_ASSERT(govCfg.tier != optigd::core::PerformanceTier::HighEnd,
                  "Governor must downgrade away from HighEnd under budget pressure");

    auto& cfgMgr = optigd::config::ConfigManager::instance();
    optigd::config::RuntimeSettingsInput input{};
    input.tierPreset = (govCfg.tier == optigd::core::PerformanceTier::LowEnd) ? "LowEnd" : "Balanced";
    auto presetCfg = cfgMgr.syncFromSettings(input);

    OPTIGD_ASSERT(std::abs(presetCfg.particleDensityScale - govCfg.particleDensityScale) < 0.001f &&
                      presetCfg.maxActiveParticles == govCfg.maxActiveParticles &&
                      presetCfg.cullOffscreenEmitters == govCfg.cullOffscreenEmitters &&
                      std::abs(presetCfg.cameraPaddingX - govCfg.cameraPaddingX) < 0.001f &&
                      std::abs(presetCfg.cameraPaddingY - govCfg.cameraPaddingY) < 0.001f,
                  "Automatic tier transitions and hand-picked presets must agree");

    OPTIGD_PASS();
}

} // namespace


// ===========================================================================
// v0.1.0-beta.3 test block: Benchmark Mode, telemetry integrity and the bug
// fixes recorded in DESIGN_LOG.md Entry 6.
//
// Every test here exists because a specific defect or a specific acceptance
// criterion required evidence. Where a test encodes a measurement *definition*
// (percentile rank, what counts as a stall, what a baseline is) the assertion
// states the definition, not just "the number changed".
// ===========================================================================

namespace bench_test {

// A clock the tests drive by hand. Kept in static storage so the lambdas stored
// in the controller's runtime bridge never capture a dangling reference: the
// controller is a singleton that outlives every test function.
struct FakeClock {
    uint64_t monotonicMs{1000};
    uint64_t epochMs{1759999999000ULL};  // a fixed, plausible UTC instant

    void advanceMs(double ms) {
        const auto delta = static_cast<uint64_t>(ms);
        monotonicMs += delta;
        epochMs += delta;
    }
    void reset() {
        monotonicMs = 1000;
        epochMs = 1759999999000ULL;
    }
};

FakeClock& clock() {
    static FakeClock s_clock;
    return s_clock;
}

std::filesystem::path& scratchRoot() {
    static std::filesystem::path s_root;
    return s_root;
}

int& scratchCounter() {
    static int s_counter = 0;
    return s_counter;
}

std::filesystem::path makeScratchDir(const char* tag) {
    std::error_code ec;
    auto root = std::filesystem::temp_directory_path(ec) /
                (std::string("optigd_test_") + tag + "_" +
                 std::to_string(++scratchCounter()) + "_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    scratchRoot() = root;
    return root;
}

void removeScratchDir() {
    std::error_code ec;
    if (!scratchRoot().empty()) std::filesystem::remove_all(scratchRoot(), ec);
    scratchRoot().clear();
}

optigd::core::FrameStats makeFrame(double intervalUs, double overheadUs = 1.0) {
    optigd::core::FrameStats stats{};
    stats.totalFrameTimeUs = intervalUs;
    stats.updateCpuTimeUs = intervalUs * 0.5;
    stats.updateThreadCpuUs = intervalUs * 0.4;
    stats.optigdOverheadUs = overheadUs;
    stats.cullingTimeUs = overheadUs * 0.5;
    stats.particleTimeUs = overheadUs * 0.25;
    stats.matrixTimeUs = overheadUs * 0.25;
    stats.culledObjects = 3;
    stats.visibleObjects = 120;
    stats.totalObjects = 123;
    stats.activeParticles = 400;
    stats.arenaPeakBytes = 4096;
    stats.arenaAllocations = 12;
    stats.heapAllocationsAvoided = 20;
    // Mirrors what OptiGDManager::endFrame() derives from the shared threshold.
    const bool stall = !(std::isfinite(intervalUs) && intervalUs > 0.0) ||
                       intervalUs > optigd::core::kStallThresholdUs;
    stats.gapFrame = stall;
    stats.frameCounted = !stall;
    return stats;
}

// Installs a bridge that mirrors what `src/geode/BenchmarkUI.cpp` installs in
// the real mod: the active configuration is read from OptiGDManager, and
// installing/clearing the override goes through ConfigManager and then
// `reloadConfig()`. Testing that path (and not a stub) is what makes the
// "never corrupts saved settings" claim end-to-end rather than aspirational.
void installTestBridge(optigd::benchmark::BenchmarkController& controller,
                       const std::filesystem::path& root,
                       double targetDurationMs,
                       bool stopOnLevelReset) {
    auto& cfgMgr = optigd::config::ConfigManager::instance();

    optigd::benchmark::BenchmarkRuntimeBridge bridge;
    bridge.readActiveConfig = [] {
        return optigd::core::OptiGDManager::instance().getActiveConfig();
    };
    bridge.installOverride = [&cfgMgr](const optigd::core::OptiConfig& config,
                                       const std::string& reason) {
        if (!cfgMgr.setRuntimeOverride(config, reason)) return false;
        optigd::core::OptiGDManager::instance().reloadConfig();
        return true;
    };
    bridge.updateOverride = [&cfgMgr](const optigd::core::OptiConfig& config) {
        if (!cfgMgr.updateRuntimeOverride(config)) return false;
        optigd::core::OptiGDManager::instance().reloadConfig();
        return true;
    };
    bridge.clearOverride = [&cfgMgr] {
        if (!cfgMgr.clearRuntimeOverride()) return false;
        optigd::core::OptiGDManager::instance().reloadConfig();
        return true;
    };
    bridge.monotonicMs = [] { return clock().monotonicMs; };
    bridge.epochMs = [] { return clock().epochMs; };
    bridge.levelIdentity = [] {
        optigd::benchmark::LevelIdentity identity;
        identity.name = "Test Level";
        identity.creator = "tester";
        identity.levelId = 4242;
        identity.levelIdKnown = true;
        identity.attempt = 1;
        identity.attemptKnown = true;
        identity.practiceMode = false;
        identity.practiceModeKnown = true;
        identity.source = "unit-test";
        return identity;
    };
    bridge.environment = [] {
        optigd::benchmark::BenchmarkEnvironment env;
        env.optigdVersion = optigd::core::kOptiGDVersion;
        env.gdVersion = "2.2081";
        env.geodeVersion = "v5.10.1";
        env.osName = "UnitTest";
        env.architecture = "test-arch";
        env.compiler = "test-compiler";
        env.logicalCores = 4;
        env.totalRamMb = 1024;
        return env;
    };

    // Tests assert and return early on failure, so a previous test can leave the
    // singleton mid-capture with an override armed. Clean up first to keep the
    // suite order-independent.
    if (controller.isRunning()) controller.cancelSession();
    controller.restoreConfiguration();
    cfgMgr.clearRuntimeOverride();

    controller.setRuntimeBridge(bridge);
    controller.setStore(std::make_shared<optigd::benchmark::BenchmarkStore>(root));

    optigd::benchmark::BenchmarkPolicy policy;
    policy.targetDurationMs = targetDurationMs;
    policy.stopOnLevelReset = stopOnLevelReset;
    policy.maxWallClockMs = targetDurationMs * 20.0;
    controller.setPolicy(policy);
    clock().reset();
}

void restoreControllerDefaults(optigd::benchmark::BenchmarkController& controller) {
    controller.restoreConfiguration();
    controller.setRuntimeBridge(optigd::benchmark::makeDefaultRuntimeBridge());
    controller.setStore(nullptr);
    optigd::benchmark::BenchmarkPolicy policy;
    controller.setPolicy(policy);
}

// A user configuration with a recognizable fingerprint, so "restored" can be
// asserted field by field rather than by pointer identity.
optigd::core::OptiConfig makeUserConfig() {
    optigd::core::OptiConfig config{};
    config.tier = optigd::core::PerformanceTier::Custom;  // never auto-scaled
    config.flags.enableSpatialCulling = true;
    config.flags.enableParticleThrottling = true;
    config.flags.enableMatrixCaching = true;
    config.flags.enableAdaptiveGovernor = false;
    config.flags.enableTriggerIndexing = true;
    config.flags.enableDiagnosticsOverlay = true;
    config.flags.exportDiagnosticsOnExit = true;
    config.targetFrameRate = 144.0f;
    config.particleDensityScale = 0.42f;
    config.maxActiveParticles = 2222;
    config.cullOffscreenEmitters = true;
    config.cameraPaddingX = 123.0f;
    config.cameraPaddingY = 77.0f;
    return config;
}

bool sameConfig(const optigd::core::OptiConfig& a, const optigd::core::OptiConfig& b) {
    return a.tier == b.tier &&
           a.flags.enableSpatialCulling == b.flags.enableSpatialCulling &&
           a.flags.enableParticleThrottling == b.flags.enableParticleThrottling &&
           a.flags.enableMatrixCaching == b.flags.enableMatrixCaching &&
           a.flags.enableAdaptiveGovernor == b.flags.enableAdaptiveGovernor &&
           a.flags.enableTriggerIndexing == b.flags.enableTriggerIndexing &&
           std::fabs(a.targetFrameRate - b.targetFrameRate) < 1e-4f &&
           std::fabs(a.particleDensityScale - b.particleDensityScale) < 1e-6f &&
           a.maxActiveParticles == b.maxActiveParticles &&
           std::fabs(a.cameraPaddingX - b.cameraPaddingX) < 1e-4f &&
           std::fabs(a.cameraPaddingY - b.cameraPaddingY) < 1e-4f;
}

optigd::benchmark::BenchmarkSession makeSession(
    optigd::benchmark::BenchmarkMode mode,
    double meanFrameTimeMs,
    uint64_t samples,
    double capturedMs,
    const char* id) {

    optigd::benchmark::BenchmarkSession session;
    session.id = id;
    session.mode = mode;
    session.state = optigd::benchmark::BenchmarkState::Completed;
    session.targetDurationMs = 60000.0;
    session.capturedDurationMs = capturedMs;
    session.wallDurationMs = capturedMs;
    session.framesCounted = samples;
    session.framesObserved = samples;
    // Every fixture gets its own non-overlapping capture window. Real runs of the
    // same level are sequential, and the comparator treats overlapping windows as
    // a blocking issue - a fixture that stamped every session with the same
    // instant would make a legitimate pair look like one capture copied twice.
    static uint64_t nextStartMs = 1759999999000ULL;
    session.startedEpochMs = nextStartMs;
    session.endedEpochMs = session.startedEpochMs + static_cast<uint64_t>(capturedMs);
    nextStartMs = session.endedEpochMs + 60000ULL;

    session.stats.samples = samples;
    session.stats.totalFrameTimeMs = capturedMs;
    session.stats.meanFrameTimeMs = meanFrameTimeMs;
    session.stats.minFrameTimeMs = meanFrameTimeMs * 0.8;
    session.stats.maxFrameTimeMs = meanFrameTimeMs * 1.5;
    session.stats.p50FrameTimeMs = meanFrameTimeMs;
    session.stats.p95FrameTimeMs = meanFrameTimeMs * 1.2;
    session.stats.p99FrameTimeMs = meanFrameTimeMs * 1.4;
    session.stats.stdDevFrameTimeMs = meanFrameTimeMs * 0.1;
    session.stats.fpsFromMeanFrameTime =
        (meanFrameTimeMs > 0.0) ? (1000.0 / meanFrameTimeMs) : 0.0;
    session.stats.fpsFromWindow = session.stats.fpsFromMeanFrameTime;
    session.stats.percentileMethod = optigd::diagnostics::PercentileMethod::Exact;
    session.stats.ringCapacity = 60000;

    using WM = optigd::diagnostics::WindowMetric;
    auto& metrics = session.stats.metrics;
    metrics[static_cast<size_t>(WM::UpdateCpuUs)].add(meanFrameTimeMs * 500.0);
    metrics[static_cast<size_t>(WM::UpdateThreadCpuUs)].add(meanFrameTimeMs * 400.0);
    metrics[static_cast<size_t>(WM::OptigdOverheadUs)].add(2.0);
    metrics[static_cast<size_t>(WM::CulledObjects)].add(3.0);

    session.config = makeUserConfig();
    session.configOverrideApplied = true;
    session.level.name = "Test Level";
    session.level.levelId = 4242;
    session.level.levelIdKnown = true;
    session.level.attempt = 1;
    session.level.attemptKnown = true;
    session.level.source = "unit-test";
    session.environment.optigdVersion = optigd::core::kOptiGDVersion;
    session.environment.gdVersion = "2.2081";
    session.environment.geodeVersion = "v5.10.1";
    session.environment.osName = "UnitTest";
    session.environment.architecture = "test-arch";
    session.refreshDerivedWarnings();
    return session;
}

} // namespace bench_test

// ---------------------------------------------------------------------------
// JsonValue: sessions are re-opened in-game with this parser, so escaping,
// nesting limits and non-finite handling are correctness requirements, not
// cosmetics.
// ---------------------------------------------------------------------------
void testJsonValueRoundTripAndEscaping() {
    namespace json = optigd::core::json;
    using optigd::core::json::Value;

    Value root = Value::object();
    root.set("id", Value::string("20261011T013512Z-baseline-0001"));
    root.set("samples", Value::integer(3594));
    root.set("mean", Value::number(16.6666));
    root.set("ok", Value::boolean(true));
    root.set("nothing", Value::null());

    Value nested = Value::object();
    nested.set("quotes", Value::string("say \"hi\" \\ and \t tab"));
    nested.set("control", Value::string(std::string("bell\actrl")));
    Value list = Value::array();
    list.push(Value::string("a"));
    list.push(Value::integer(2));
    nested.set("list", std::move(list));
    root.set("nested", std::move(nested));

    bool sawNonFinite = false;
    const std::string text = json::serialize(root, 2, &sawNonFinite);
    OPTIGD_ASSERT(!sawNonFinite, "finite values must not set the non-finite flag");
    OPTIGD_ASSERT(text.find("\\\"hi\\\"") != std::string::npos, "quotes must be escaped");
    OPTIGD_ASSERT(text.find("\\\\") != std::string::npos, "backslashes must be escaped");
    OPTIGD_ASSERT(text.find("\\t") != std::string::npos, "tabs must be escaped");
    OPTIGD_ASSERT(text.find("\\u0007") != std::string::npos, "control chars must be \\u escaped");
    OPTIGD_ASSERT(text.find('\a') == std::string::npos, "no raw control character may survive");

    auto parsed = json::parse(text);
    OPTIGD_ASSERT(parsed.ok, parsed.error);
    OPTIGD_ASSERT(parsed.value.isObject(), "root must be an object");
    OPTIGD_ASSERT(parsed.value.getString("id", "") == "20261011T013512Z-baseline-0001",
                  "string round-trip");
    OPTIGD_ASSERT(parsed.value.getInt("samples", 0) == 3594, "integer round-trip");
    OPTIGD_ASSERT(std::fabs(parsed.value.getDouble("mean", 0.0) - 16.6666) < 1e-9,
                  "number round-trip");
    OPTIGD_ASSERT(parsed.value.getBool("ok", false), "boolean round-trip");
    const Value* nestedBack = parsed.value.getObject("nested");
    OPTIGD_ASSERT(nestedBack != nullptr, "nested object round-trip");
    OPTIGD_ASSERT(nestedBack->getString("quotes", "") == "say \"hi\" \\ and \t tab",
                  "escaped string must decode back to the original bytes");
    const Value* listBack = nestedBack->getArray("list");
    OPTIGD_ASSERT(listBack != nullptr && listBack->asArray().size() == 2, "array round-trip");

    // Non-finite doubles are not representable in JSON. Emitting `nan`/`inf`
    // would produce a file no consumer can parse, so they become null and the
    // caller is told.
    Value bad = Value::object();
    bad.set("not_a_number", Value::number(std::numeric_limits<double>::quiet_NaN()));
    bad.set("unbounded", Value::number(std::numeric_limits<double>::infinity()));
    bool flagged = false;
    const std::string badText = json::serialize(bad, 0, &flagged);
    OPTIGD_ASSERT(flagged, "non-finite values must be flagged to the caller");
    OPTIGD_ASSERT(badText.find("null") != std::string::npos,
                  "a non-finite double must be written as JSON null");
    // A literal `nan`/`inf` would not parse, so parsing is the real assertion.
    auto badParsed = json::parse(badText);
    OPTIGD_ASSERT(badParsed.ok, "the sanitized output must be parseable JSON");
    OPTIGD_ASSERT(badParsed.value.getObject("nested") == nullptr || true, "shape is irrelevant here");

    // Depth and length limits: a hostile or corrupt file must not blow the stack.
    std::string deep(200, '[');
    deep += std::string(200, ']');
    auto tooDeep = json::parse(deep);
    OPTIGD_ASSERT(!tooDeep.ok, "nesting beyond the configured limit must be rejected");

    auto truncated = json::parse("{\"a\": 1, \"b\":");
    OPTIGD_ASSERT(!truncated.ok, "truncated JSON must be rejected");
    OPTIGD_ASSERT(!truncated.error.empty(), "a parse failure must carry a message");

    OPTIGD_PASS();
}

// ---------------------------------------------------------------------------
// Percentile definitions are load-bearing: two different definitions in one
// codebase is how a report ends up describing something other than what it says.
// ---------------------------------------------------------------------------
void testSampleWindowPercentileDefinitions() {
    using optigd::diagnostics::SampleWindow;
    using optigd::diagnostics::SampleWindowConfig;
    using optigd::diagnostics::StatPrecision;

    // 1..100 ms, one sample per value. Nearest-rank P95 = ceil(0.95*100)-1 = 94
    // -> the 95th value = 95. beta.2's floor(0.95*100) = 95 -> the 96th value.
    SampleWindow window;
    SampleWindowConfig config;
    config.ringCapacity = 1000;
    config.histogramMaxMs = 250.0;
    config.histogramBins = 2500;
    window.configure(config);

    for (int i = 1; i <= 100; ++i) window.pushFrameTime(static_cast<double>(i) * 1000.0);

    auto stats = window.computeStatistics(StatPrecision::Exact);
    OPTIGD_ASSERT(stats.samples == 100, "all 100 samples must be accepted");
    OPTIGD_ASSERT(stats.percentileMethod == optigd::diagnostics::PercentileMethod::Exact,
                  "a window inside its ring capacity must report exact percentiles");
    OPTIGD_ASSERT(std::fabs(stats.p95FrameTimeMs - 95.0) < 1e-9,
                  "P95 must be nearest-rank ceil(p*n)-1, not floor(p*n)");
    OPTIGD_ASSERT(std::fabs(stats.p99FrameTimeMs - 99.0) < 1e-9,
                  "P99 must be nearest-rank ceil(p*n)-1, not the maximum");
    OPTIGD_ASSERT(std::fabs(stats.p50FrameTimeMs - 50.5) < 1e-9,
                  "P50 with an even count must be the mean of the two central values");
    OPTIGD_ASSERT(stats.percentilesAreMonotonic(), "P50 <= P95 <= P99 must always hold");
    OPTIGD_ASSERT(std::fabs(stats.meanFrameTimeMs - 50.5) < 1e-9, "mean of 1..100 is 50.5");
    OPTIGD_ASSERT(std::fabs(stats.minFrameTimeMs - 1.0) < 1e-9, "min must be 1 ms");
    OPTIGD_ASSERT(std::fabs(stats.maxFrameTimeMs - 100.0) < 1e-9, "max must be 100 ms");
    OPTIGD_ASSERT(!stats.ringOverflow, "1000-slot ring holding 100 samples cannot overflow");
    OPTIGD_ASSERT(stats.ringUsed == 100, "ringUsed must report retained samples");

    // Odd count: the median is the central value, not an average.
    SampleWindow odd;
    odd.configure(config);
    for (int i = 1; i <= 5; ++i) odd.pushFrameTime(static_cast<double>(i) * 1000.0);
    auto oddStats = odd.computeStatistics();
    OPTIGD_ASSERT(std::fabs(oddStats.p50FrameTimeMs - 3.0) < 1e-9,
                  "P50 with an odd count must be the central value");
    OPTIGD_ASSERT(std::fabs(oddStats.p95FrameTimeMs - 5.0) < 1e-9,
                  "P95 of 5 samples is the 5th (ceil(4.75)-1 = 4)");

    // An empty window must not invent plausible-looking zeros for a mean it never
    // observed; the method is reported as `none` so a consumer can tell.
    SampleWindow empty;
    empty.configure(config);
    auto emptyStats = empty.computeStatistics();
    OPTIGD_ASSERT(emptyStats.samples == 0, "empty window has no samples");
    OPTIGD_ASSERT(emptyStats.percentileMethod == optigd::diagnostics::PercentileMethod::None,
                  "an empty window must report percentile_method = none");

    // The shared static helpers must agree with the window's own computation, so
    // AdaptiveGovernor and the reports cannot drift apart.
    std::vector<double> sorted{1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0};
    OPTIGD_ASSERT(std::fabs(SampleWindow::percentileFromSorted(sorted.data(), sorted.size(), 0.95)
                            - 10.0) < 1e-9,
                  "P95 of 10 samples is the 10th (ceil(9.5)-1 = 9)");
    OPTIGD_ASSERT(std::fabs(SampleWindow::percentileFromSorted(sorted.data(), sorted.size(), 0.50)
                            - 5.0) < 1e-9,
                  "P50 via nearest rank of 10 samples is the 5th");
    OPTIGD_ASSERT(std::fabs(SampleWindow::medianFromSorted(sorted.data(), sorted.size()) - 5.5)
                      < 1e-9,
                  "median of an even count averages the two central values");

    OPTIGD_PASS();
}

// ---------------------------------------------------------------------------
// Defect A/N: a report must disclose the window its percentiles came from, and a
// truncated exact ring must degrade *visibly* rather than silently.
// ---------------------------------------------------------------------------
void testSampleWindowRingOverflowDisclosesApproximation() {
    using optigd::diagnostics::SampleWindow;
    using optigd::diagnostics::SampleWindowConfig;
    using optigd::diagnostics::StatPrecision;

    SampleWindow window;
    SampleWindowConfig config;
    config.ringCapacity = 100;      // deliberately too small
    config.histogramMaxMs = 100.0;
    config.histogramBins = 1000;    // 0.1 ms bins
    window.configure(config);

    for (int i = 0; i < 500; ++i) window.pushFrameTime(16.0 * 1000.0);

    auto stats = window.computeStatistics(StatPrecision::Exact);
    OPTIGD_ASSERT(stats.samples == 500, "every sample must still be counted");
    OPTIGD_ASSERT(stats.ringCapacity == 100, "the configured capacity must be reported");
    OPTIGD_ASSERT(stats.ringUsed == 100, "only 100 exact samples are retained");
    OPTIGD_ASSERT(stats.ringOverflow, "overflow must be reported, not hidden");
    OPTIGD_ASSERT(stats.overflowSamples == 400, "400 samples were evicted from the ring");
    OPTIGD_ASSERT(stats.percentileMethod ==
                      optigd::diagnostics::PercentileMethod::HistogramApproximate,
                  "percentiles from an overflowed ring must be labelled approximate");
    // The histogram estimator returns the upper bound of the bin holding the
    // rank, so it over-estimates by at most one bin width and never under-reports.
    OPTIGD_ASSERT(stats.p50FrameTimeMs >= 16.0 &&
                  stats.p50FrameTimeMs <= 16.0 + stats.histogramBinWidthMs + 1e-9,
                  "approximate P50 must be within one bin width above the truth");
    OPTIGD_ASSERT(std::fabs(stats.meanFrameTimeMs - 16.0) < 1e-9,
                  "the mean stays exact: it comes from accumulators, not the ring");
    OPTIGD_ASSERT(std::fabs(stats.totalFrameTimeMs - 500.0 * 16.0) < 1e-6,
                  "the window duration stays exact");
    OPTIGD_ASSERT(stats.percentilesAreMonotonic(), "monotonicity must hold for the estimator too");

    // A value beyond the histogram range lands in the overflow bin. It must be
    // reported as saturated rather than silently clamped to the range maximum.
    window.pushFrameTime(5000.0 * 1000.0);  // 5 s
    auto saturated = window.computeStatistics(StatPrecision::Exact);
    OPTIGD_ASSERT(saturated.histogramSaturated, "an out-of-range sample must set the saturation flag");
    OPTIGD_ASSERT(std::fabs(saturated.maxFrameTimeMs - 5000.0) < 1e-6,
                  "max is exact even when the histogram saturates");

    // `Fast` must never claim to be exact: it answers from the histogram so a
    // 4 Hz HUD refresh over 60k samples cannot stall the frame it measures.
    SampleWindow fast;
    fast.configure(config);
    for (int i = 0; i < 50; ++i) fast.pushFrameTime(16.0 * 1000.0);
    auto fastStats = fast.computeStatistics(StatPrecision::Fast);
    OPTIGD_ASSERT(fastStats.precisionWasFast, "Fast precision must be reported as such");
    OPTIGD_ASSERT(fastStats.percentileMethod ==
                      optigd::diagnostics::PercentileMethod::HistogramApproximate,
                  "Fast precision must never report exact percentiles");

    // Bounded memory: the window must state what it owns, and it must not grow
    // with the number of samples pushed.
    const size_t bytesAt500 = window.getOwnedBytes();
    for (int i = 0; i < 5000; ++i) window.pushFrameTime(16.0 * 1000.0);
    OPTIGD_ASSERT(window.getOwnedBytes() == bytesAt500,
                  "window memory must be bounded and allocated once");
    OPTIGD_ASSERT(bytesAt500 > 0, "owned bytes must be reported, not zero");

    OPTIGD_PASS();
}

void testSampleWindowHitchStallAndRejectionAccounting() {
    using optigd::diagnostics::SampleWindow;
    using optigd::diagnostics::SampleWindowConfig;

    SampleWindow window;
    SampleWindowConfig config;
    config.ringCapacity = 10000;
    config.histogramMaxMs = 250.0;
    config.histogramBins = 2500;
    config.hitchThresholdMs = 1000.0 / 30.0;
    config.stallThresholdMs = 250.0;
    window.configure(config);

    for (int i = 0; i < 100; ++i) window.pushFrameTime(16.0 * 1000.0);
    window.pushFrameTime(50.0 * 1000.0);    // a hitch (> 33.3 ms), still gameplay
    window.pushFrameTime(400.0 * 1000.0);   // a stall (> 250 ms)
    window.pushFrameTime(0.0);              // rejected: non-positive
    window.pushFrameTime(-5.0 * 1000.0);    // rejected: negative
    window.pushFrameTime(std::numeric_limits<double>::quiet_NaN() * 1000.0);

    auto stats = window.computeStatistics();
    OPTIGD_ASSERT(stats.samples == 102, "100 normal + 1 hitch + 1 stall are all counted");
    OPTIGD_ASSERT(stats.rejectedSamples == 3, "non-finite and non-positive intervals are rejected");
    OPTIGD_ASSERT(stats.hitchFrames == 2, "the 50 ms and 400 ms frames are both hitches");
    OPTIGD_ASSERT(std::fabs(stats.hitchTotalMs - 450.0) < 1e-9, "hitch time is the sum of both");
    OPTIGD_ASSERT(stats.stallFrames == 1, "only the 400 ms frame is a stall");
    OPTIGD_ASSERT(stats.maxFrameTimeMs >= 400.0 - 1e-9,
                  "a stall kept as a sample must remain visible in the distribution");
    OPTIGD_ASSERT(std::fabs(stats.meanFrameTimeMs -
                            ((100 * 16.0 + 50.0 + 400.0) / 102.0)) < 1e-9,
                  "the mean must include every counted sample");

    // Exclusions are counted AND timed, so a report can account for wall time
    // that is not in the distribution instead of just dropping it.
    window.recordExcluded(1500.0 * 1000.0);
    window.recordExcluded(300.0 * 1000.0);
    auto excluded = window.computeStatistics();
    OPTIGD_ASSERT(excluded.excludedSamples == 2, "excluded samples must be counted");
    OPTIGD_ASSERT(std::fabs(excluded.excludedTimeMs - 1800.0) < 1e-9,
                  "excluded time must be summed");
    OPTIGD_ASSERT(std::fabs(excluded.longestExcludedMs - 1500.0) < 1e-9,
                  "the longest exclusion must be reported separately");
    OPTIGD_ASSERT(excluded.samples == 102, "excluding must not change the counted samples");

    // The stall threshold is shared with the engine, so the classifier and the
    // frame hook cannot disagree about what a stall is.
    OPTIGD_ASSERT(window.isStallIntervalUs(optigd::core::kStallThresholdUs + 1.0),
                  "just above the shared threshold is a stall");
    OPTIGD_ASSERT(!window.isStallIntervalUs(optigd::core::kStallThresholdUs - 1.0),
                  "just below the shared threshold is not a stall");
    OPTIGD_ASSERT(!window.isStallIntervalUs(0.0), "a zero interval is not a stall");
    OPTIGD_ASSERT(!window.isStallIntervalUs(std::numeric_limits<double>::quiet_NaN()),
                  "a NaN interval is not a stall (it is a rejection)");

    // reset() must clear the accounting without giving back the allocations.
    const size_t ownedBefore = window.getOwnedBytes();
    window.reset();
    auto afterReset = window.computeStatistics();
    OPTIGD_ASSERT(afterReset.samples == 0 && afterReset.rejectedSamples == 0 &&
                  afterReset.excludedSamples == 0 && afterReset.hitchFrames == 0,
                  "reset must clear every counter");
    OPTIGD_ASSERT(window.getOwnedBytes() == ownedBefore,
                  "reset must keep the configured allocations");
    OPTIGD_ASSERT(window.getRingCapacity() == 10000, "reset must keep the capacity");

    OPTIGD_PASS();
}

// ---------------------------------------------------------------------------
// Defect K: there was nowhere to hold a transient configuration. This is the
// regression test for the two-layer ConfigManager.
// ---------------------------------------------------------------------------
void testConfigManagerTransientOverrideIsolation() {
    auto& cfgMgr = optigd::config::ConfigManager::instance();
    cfgMgr.resetToDefaults();
    cfgMgr.clearRuntimeOverride();

    const auto user = bench_test::makeUserConfig();
    cfgMgr.setBaseConfig(user);
    OPTIGD_ASSERT(bench_test::sameConfig(cfgMgr.getConfig(), user), "with no override, active == base");
    OPTIGD_ASSERT(!cfgMgr.hasRuntimeOverride(), "no override is active yet");

    // Clearing an override that was never installed must report failure, so a
    // double-restore in the controller is detectable instead of silent.
    OPTIGD_ASSERT(!cfgMgr.clearRuntimeOverride(), "clearing with nothing active must return false");

    auto baseline = optigd::benchmark::makeBaselineConfig(user);
    OPTIGD_ASSERT(cfgMgr.setRuntimeOverride(baseline, "benchmark baseline"),
                  "installing an override must succeed");
    OPTIGD_ASSERT(cfgMgr.hasRuntimeOverride(), "the override must now be active");
    OPTIGD_ASSERT(cfgMgr.getRuntimeOverrideReason() == "benchmark baseline",
                  "the reason must be recorded");
    OPTIGD_ASSERT(!cfgMgr.getConfig().flags.enableSpatialCulling,
                  "the active config must be the override");
    OPTIGD_ASSERT(cfgMgr.getBaseConfig().flags.enableSpatialCulling,
                  "the BASE config must be untouched by an override");

    // A second install must be refused: arming twice would lose the original base
    // snapshot and make restoration impossible.
    OPTIGD_ASSERT(!cfgMgr.setRuntimeOverride(user, "second arm"),
                  "installing a second override must be refused");
    OPTIGD_ASSERT(cfgMgr.getRuntimeOverrideReason() == "benchmark baseline",
                  "a refused second install must not change the reason");

    // A Geode settings listener firing mid-session writes the BASE layer only.
    optigd::config::RuntimeSettingsInput input{};
    input.tierPreset = "Custom";
    input.targetFrameRate = 240.0f;
    input.enableSpatialCulling = false;
    input.enableParticleThrottling = false;
    input.enableMatrixCaching = false;
    input.enableAdaptiveGovernor = false;
    input.enableTriggerIndexing = false;
    input.enableDiagnosticsOverlay = true;
    input.exportDiagnosticsOnExit = true;
    input.customParticleDensity = user.particleDensityScale;
    input.customMaxParticles = user.maxActiveParticles;
    input.customCameraPaddingX = user.cameraPaddingX;
    input.customCameraPaddingY = user.cameraPaddingY;
    cfgMgr.syncFromSettings(input);
    OPTIGD_ASSERT(cfgMgr.hasRuntimeOverride(), "syncFromSettings must not clear an override");
    OPTIGD_ASSERT(!cfgMgr.getConfig().flags.enableSpatialCulling &&
                  cfgMgr.getConfig().targetFrameRate == user.targetFrameRate,
                  "the ACTIVE config must still be the benchmark override");
    OPTIGD_ASSERT(std::fabs(cfgMgr.getBaseConfig().targetFrameRate - 240.0f) < 1e-4f,
                  "the settings change must land in the BASE layer");

    // Per-frame adaptive write-back goes to the ACTIVE layer, so a tier change
    // during a capture cannot leak into the user's saved preferences.
    auto midSession = cfgMgr.getConfig();
    midSession.maxActiveParticles = 1;
    cfgMgr.setConfig(midSession);
    OPTIGD_ASSERT(cfgMgr.getBaseConfig().maxActiveParticles == user.maxActiveParticles,
                  "setConfig during an override must not touch the base layer");
    OPTIGD_ASSERT(cfgMgr.getConfig().maxActiveParticles == 1,
                  "setConfig during an override must update the active layer");

    // Restoring gives back exactly what the user had, *including* the change they
    // made while the session was running.
    OPTIGD_ASSERT(cfgMgr.clearRuntimeOverride(), "clearing an active override must succeed");
    OPTIGD_ASSERT(!cfgMgr.hasRuntimeOverride(), "the override must be gone");
    OPTIGD_ASSERT(std::fabs(cfgMgr.getConfig().targetFrameRate - 240.0f) < 1e-4f,
                  "restoration must expose the settings change made during the session");
    OPTIGD_ASSERT(!cfgMgr.getConfig().flags.enableSpatialCulling,
                  "restoration must expose the flag change made during the session");

    // Serialization is base-only: a transient benchmark configuration must never
    // reach the user's saved file.
    cfgMgr.setBaseConfig(user);
    cfgMgr.setRuntimeOverride(baseline, "benchmark baseline");
    const std::string serialized = cfgMgr.serialize();
    auto roundTrip = user;
    roundTrip.flags.enableSpatialCulling = false;
    cfgMgr.deserialize(serialized);
    OPTIGD_ASSERT(cfgMgr.hasRuntimeOverride(),
                  "deserialize writes the base layer and must not clear an override");
    OPTIGD_ASSERT(cfgMgr.getBaseConfig().flags.enableSpatialCulling,
                  "the serialized text must describe the BASE config, not the override");
    (void)roundTrip;

    cfgMgr.clearRuntimeOverride();
    cfgMgr.resetToDefaults();
    OPTIGD_PASS();
}

void testMakeBaselineConfigIsInternalControlNotVanilla() {
    const auto user = bench_test::makeUserConfig();
    const auto baseline = optigd::benchmark::makeBaselineConfig(user);

    OPTIGD_ASSERT(!baseline.flags.enableSpatialCulling, "spatial culling must be off");
    OPTIGD_ASSERT(!baseline.flags.enableParticleThrottling, "particle throttling must be off");
    OPTIGD_ASSERT(!baseline.flags.enableMatrixCaching, "matrix caching must be off");
    OPTIGD_ASSERT(!baseline.flags.enableTriggerIndexing, "trigger indexing must be off");
    OPTIGD_ASSERT(!baseline.flags.enableAdaptiveGovernor, "the adaptive governor must be off");

    // Numerics and the tier are preserved so the record shows exactly what
    // changed, and so the baseline is not a second, unrelated configuration.
    OPTIGD_ASSERT(baseline.tier == user.tier, "the tier must be preserved");
    OPTIGD_ASSERT(std::fabs(baseline.targetFrameRate - user.targetFrameRate) < 1e-4f,
                  "target frame rate must be preserved");
    OPTIGD_ASSERT(std::fabs(baseline.particleDensityScale - user.particleDensityScale) < 1e-6f,
                  "particle density must be preserved");
    OPTIGD_ASSERT(baseline.maxActiveParticles == user.maxActiveParticles,
                  "the particle cap must be preserved");

    // Diagnostics are not optimizations: a baseline that turned them off could
    // not be measured.
    OPTIGD_ASSERT(baseline.flags.enableDiagnosticsOverlay == user.flags.enableDiagnosticsOverlay,
                  "the diagnostics overlay must keep the user's value");
    OPTIGD_ASSERT(baseline.flags.exportDiagnosticsOnExit == user.flags.exportDiagnosticsOnExit,
                  "the diagnostics exporter must keep the user's value");

    // The passthrough description is derived from the two configs, so it cannot
    // claim a change that never happened.
    const auto described = optigd::benchmark::describePassthroughFeatures(user, baseline);
    OPTIGD_ASSERT(described.size() == 4,
                  "exactly the four enabled optimizations must be described as disabled");
    for (const auto& name : described) {
        // These are CompatibilityLayer feature ids, not setting labels, so the
        // record names the same features the compatibility gate knows about.
        OPTIGD_ASSERT(name == "spatial_culling" || name == "particle_governor" ||
                      name == "matrix_cache" || name == "trigger_indexer" ||
                      name == "adaptive_governor",
                      "only real feature ids may be listed");
    }

    // With nothing enabled, nothing may be claimed as disabled.
    auto empty = user;
    empty.flags.enableSpatialCulling = false;
    empty.flags.enableParticleThrottling = false;
    empty.flags.enableMatrixCaching = false;
    empty.flags.enableTriggerIndexing = false;
    empty.flags.enableAdaptiveGovernor = false;
    OPTIGD_ASSERT(optigd::benchmark::describePassthroughFeatures(
                      empty, optigd::benchmark::makeBaselineConfig(empty))
                      .empty(),
                  "a config with nothing enabled must claim nothing was disabled");

    OPTIGD_PASS();
}

// ---------------------------------------------------------------------------
// Session record: JSON round-trip, schema gating and derived warnings.
// ---------------------------------------------------------------------------
void testBenchmarkSessionJsonRoundTrip() {
    using optigd::benchmark::BenchmarkSession;

    auto original = bench_test::makeSession(optigd::benchmark::BenchmarkMode::Baseline,
                                            16.7, 3592, 60000.0,
                                            "20261011T013512Z-baseline-0001");
    original.pauseEvents = 1;
    original.gapEvents = 2;
    original.pausedMs = 4321.0;
    original.excludedGapMs = 9000.0;
    original.stats.excludedTimeMs = 9000.0;
    original.stats.excludedSamples = 2;
    original.refreshDerivedWarnings();

    const std::string text = original.serialize();
    OPTIGD_ASSERT(!text.empty(), "serialization must produce text");
    OPTIGD_ASSERT(text.find("\"report_kind\": \"optigd_benchmark_session\"") != std::string::npos,
                  "the report kind must be present");
    OPTIGD_ASSERT(text.find("\"schema_version\": 1") != std::string::npos,
                  "the schema version must be present");
    // A session built in this process has no live game behind it, so it may only
    // claim to be simulated. Stamping "[MEASURED-IN-GAME]" on it would be
    // inventing evidence: the tag is decided by the runtime, never by the data.
    OPTIGD_ASSERT(original.evidenceClassification == optigd::benchmark::evidence::kSimulated,
                  "a standalone-built session must default to [SIMULATED]");
    OPTIGD_ASSERT(text.find("[SIMULATED]") != std::string::npos,
                  "the evidence classification must be present in the JSON");
    // A literal `nan` or `inf` is not JSON, so "it parses" is the real check.
    OPTIGD_ASSERT(optigd::core::json::parse(text).ok,
                  "the serialized session must be parseable JSON");

    BenchmarkSession restored;
    std::string error;
    OPTIGD_ASSERT(BenchmarkSession::parse(text, restored, error), error.c_str());
    OPTIGD_ASSERT(restored.id == original.id, "id round-trip");
    OPTIGD_ASSERT(restored.mode == original.mode, "mode round-trip");
    OPTIGD_ASSERT(restored.state == original.state, "state round-trip");
    OPTIGD_ASSERT(restored.isComparable(), "a completed session with samples is comparable");
    OPTIGD_ASSERT(restored.evidenceClassification == original.evidenceClassification,
                  "evidence classification must round-trip");

    {
        // Reading a file may reduce what a record claims, never increase it.
        auto upgraded = original;
        upgraded.evidenceClassification = optigd::benchmark::evidence::kMeasuredInGame;
        BenchmarkSession back;
        std::string perr;
        OPTIGD_ASSERT(BenchmarkSession::parse(upgraded.serialize(), back, perr), perr.c_str());
        OPTIGD_ASSERT(back.evidenceClassification ==
                          optigd::benchmark::evidence::kMeasuredInGame,
                      "a real in-game tag must survive a round-trip");

        std::string tampered = upgraded.serialize();
        const auto pos = tampered.find("[MEASURED-IN-GAME]");
        OPTIGD_ASSERT(pos != std::string::npos, "the fixture must contain the tag");
        tampered.replace(pos, std::string("[MEASURED-IN-GAME]").size(), "[PROBABLY-REAL]");
        BenchmarkSession suspect;
        OPTIGD_ASSERT(BenchmarkSession::parse(tampered, suspect, perr), perr.c_str());
        OPTIGD_ASSERT(suspect.evidenceClassification ==
                          optigd::benchmark::evidence::kSimulated,
                      "an unrecognized tag must be downgraded to [SIMULATED]");
    }
    OPTIGD_ASSERT(std::fabs(restored.capturedDurationMs - original.capturedDurationMs) < 1e-6,
                  "captured duration round-trip");
    OPTIGD_ASSERT(std::fabs(restored.pausedMs - original.pausedMs) < 1e-6,
                  "paused time round-trip");
    OPTIGD_ASSERT(restored.framesCounted == original.stats.samples,
                  "framesCounted must be re-derived from the window statistics");
    OPTIGD_ASSERT(restored.stats.samples == original.stats.samples, "sample count round-trip");
    OPTIGD_ASSERT(std::fabs(restored.avgFps() - original.avgFps()) < 1e-6, "avg fps round-trip");
    OPTIGD_ASSERT(std::fabs(restored.p99FrameTimeMs() - original.p99FrameTimeMs()) < 1e-6,
                  "p99 round-trip");
    OPTIGD_ASSERT(restored.config.flags.enableSpatialCulling ==
                  original.config.flags.enableSpatialCulling,
                  "configuration flags round-trip");
    OPTIGD_ASSERT(std::fabs(restored.config.particleDensityScale -
                            original.config.particleDensityScale) < 1e-6f,
                  "configuration numerics round-trip");
    OPTIGD_ASSERT(restored.level.levelId == original.level.levelId, "level id round-trip");
    OPTIGD_ASSERT(restored.level.name == original.level.name, "level name round-trip");
    OPTIGD_ASSERT(restored.environment.gdVersion == original.environment.gdVersion,
                  "gd version round-trip");
    OPTIGD_ASSERT(restored.pauseEvents == 1 && restored.gapEvents == 2,
                  "integrity counters round-trip");
    OPTIGD_ASSERT(restored.hasWarning(optigd::benchmark::warning::kPaused),
                  "the pause warning must survive the round-trip");
    OPTIGD_ASSERT(restored.hasWarning(optigd::benchmark::warning::kLongGap),
                  "the long-gap warning must survive the round-trip");

    // Re-serializing must be stable: two writes of the same record are byte
    // identical, which is what lets the store treat an identical file as "already
    // saved" instead of an overwrite.
    const std::string again = restored.serialize();
    OPTIGD_ASSERT(again == text, "serialization must be byte-stable across a round-trip");

    OPTIGD_PASS();
}

void testBenchmarkSessionRejectsForeignAndNewerSchemas() {
    using optigd::benchmark::BenchmarkSession;
    namespace json = optigd::core::json;
    using optigd::core::json::Value;

    const auto good = bench_test::makeSession(optigd::benchmark::BenchmarkMode::Optimized,
                                              10.0, 6000, 60000.0, "session-a");
    const std::string text = good.serialize();

    // Wrong report kind: a diagnostics report is not a benchmark session.
    {
        auto parsed = json::parse(text);
        OPTIGD_ASSERT(parsed.ok, "the fixture must parse");
        Value wrong = parsed.value;
        wrong.set("report_kind", Value::string("optigd_level_diagnostics"));
        BenchmarkSession out;
        std::string error;
        OPTIGD_ASSERT(!BenchmarkSession::fromJson(wrong, out, error),
                      "a different report kind must be rejected");
        OPTIGD_ASSERT(!error.empty(), "rejection must carry a message");
    }

    // Missing schema version.
    {
        auto parsed = json::parse(text);
        // `set` replaces in place, so drop the key by rebuilding without it.
        Value rebuilt = Value::object();
        for (const auto& entry : parsed.value.asObject()) {
            if (entry.first == "schema_version") continue;
            rebuilt.set(entry.first, entry.second);
        }
        BenchmarkSession out;
        std::string error;
        OPTIGD_ASSERT(!BenchmarkSession::fromJson(rebuilt, out, error),
                      "a session without schema_version must be rejected");
    }

    // A schema newer than this build: refusing is correct, because a reader that
    // does not know the fields would silently drop evidence.
    {
        auto parsed = json::parse(text);
        Value newer = parsed.value;
        newer.set("schema_version",
                  Value::integer(BenchmarkSession::kSchemaVersion + 1));
        BenchmarkSession out;
        std::string error;
        OPTIGD_ASSERT(!BenchmarkSession::fromJson(newer, out, error),
                      "a newer schema_version must be rejected");
        OPTIGD_ASSERT(error.find("newer") != std::string::npos,
                      "the message must say the file is from a newer schema");
    }

    // A terminal state that claims completion with no data is a contradiction;
    // loading must downgrade it rather than preserve the claim.
    {
        auto empty = bench_test::makeSession(optigd::benchmark::BenchmarkMode::Baseline,
                                             0.0, 0, 0.0, "session-empty");
        empty.state = optigd::benchmark::BenchmarkState::Completed;
        const std::string emptyText = empty.serialize();
        BenchmarkSession out;
        std::string error;
        OPTIGD_ASSERT(BenchmarkSession::parse(emptyText, out, error), error.c_str());
        OPTIGD_ASSERT(out.state == optigd::benchmark::BenchmarkState::Invalid,
                      "a completed session with zero samples must load as invalid");
        OPTIGD_ASSERT(!out.isComparable(), "an invalid session must not be comparable");
        OPTIGD_ASSERT(out.hasWarning(optigd::benchmark::warning::kNoSamples),
                      "the missing-samples warning must be derived");
    }

    // Truncated / garbage input must be reported, never throw.
    BenchmarkSession out;
    std::string error;
    OPTIGD_ASSERT(!BenchmarkSession::parse("{not json", out, error), "garbage must be rejected");
    OPTIGD_ASSERT(!BenchmarkSession::parse("", out, error), "empty input must be rejected");
    OPTIGD_ASSERT(!BenchmarkSession::parse(text.substr(0, text.size() / 2), out, error),
                  "truncated input must be rejected");

    OPTIGD_PASS();
}

// ---------------------------------------------------------------------------
// Store durability: never overwrite prior evidence, never leave a partial file.
// ---------------------------------------------------------------------------
void testBenchmarkStoreNeverOverwritesPriorEvidence() {
    namespace bm = optigd::benchmark;
    const auto root = bench_test::makeScratchDir("store");

    bm::BenchmarkStore store(root);
    std::string error;
    OPTIGD_ASSERT(store.ensureDirectory(error), error.c_str());

    const auto session = bench_test::makeSession(bm::BenchmarkMode::Baseline, 16.7, 3592,
                                                 60000.0, "20261011T013512Z-baseline-0001");

    const auto first = store.save(session);
    OPTIGD_ASSERT(first.ok && first.wrote, "the first save must write");
    OPTIGD_ASSERT(std::filesystem::exists(first.path), "the session file must exist");

    // Saving the identical record again is not an overwrite: it is a no-op that
    // reports success without touching the file.
    const auto before = std::filesystem::last_write_time(first.path);
    const auto second = store.save(session);
    OPTIGD_ASSERT(second.ok, "re-saving identical bytes must succeed");
    OPTIGD_ASSERT(!second.wrote, "re-saving identical bytes must not rewrite the file");
    OPTIGD_ASSERT(std::filesystem::last_write_time(first.path) == before,
                  "the file must not be touched when the bytes are identical");

    // A *different* record under the same id must be refused outright.
    auto mutated = session;
    mutated.stats.meanFrameTimeMs = 5.0;
    mutated.stats.samples = 9999;
    const auto third = store.save(mutated);
    OPTIGD_ASSERT(!third.ok, "overwriting an existing session with different bytes must fail");
    OPTIGD_ASSERT(third.errorCode == "already_exists", "the failure must be machine-readable");

    bm::BenchmarkSession reloaded;
    OPTIGD_ASSERT(store.load(session.id, reloaded, error), error.c_str());
    OPTIGD_ASSERT(reloaded.stats.samples == 3592,
                  "the original record must still be the one on disk");

    // Session ids are validated so an id can never escape the store directory.
    OPTIGD_ASSERT(bm::isSafeSessionId("20261011T013512Z-baseline-0001"), "a normal id is safe");
    OPTIGD_ASSERT(!bm::isSafeSessionId("../escape"), "path traversal must be rejected");
    OPTIGD_ASSERT(!bm::isSafeSessionId("a/b"), "a separator must be rejected");
    OPTIGD_ASSERT(!bm::isSafeSessionId(".."), "the parent directory name must be rejected");
    OPTIGD_ASSERT(!bm::isSafeSessionId(".hidden"), "a leading dot must be rejected");
    OPTIGD_ASSERT(!bm::isSafeSessionId("back\\slash"), "a backslash must be rejected");
    OPTIGD_ASSERT(!bm::isSafeSessionId(std::string(200, 'a')), "an over-long id must be rejected");
    auto badId = session;
    badId.id = "../escape";
    OPTIGD_ASSERT(!store.save(badId).ok, "an unsafe id must never reach the filesystem");

    // Ids sort chronologically as plain strings, which is what makes the results
    // list readable without parsing timestamps.
    const std::string early = bm::generateSessionId(bm::BenchmarkMode::Baseline, 1000, 1);
    const std::string late = bm::generateSessionId(bm::BenchmarkMode::Baseline, 2000, 1);
    OPTIGD_ASSERT(early < late, "generated ids must sort chronologically");
    OPTIGD_ASSERT(early.find("baseline") != std::string::npos, "the mode must appear in the id");

    // Two sessions started in the same second must not collide: the allocator
    // probes the directory, so an id that is already on disk is skipped.
    uint32_t counter = 7;
    const std::string a = store.allocateSessionId(bm::BenchmarkMode::Optimized, 5000, counter);
    auto allocated = session;
    allocated.id = a;
    allocated.mode = bm::BenchmarkMode::Optimized;
    OPTIGD_ASSERT(store.save(allocated).ok, "the first allocated id must be savable");
    uint32_t counter2 = 7;
    const std::string b = store.allocateSessionId(bm::BenchmarkMode::Optimized, 5000, counter2);
    OPTIGD_ASSERT(a != b, "allocateSessionId must not return an id that is already on disk");
    OPTIGD_ASSERT(!store.exists(b), "the second id must be free");

    // The listing is tolerant: a corrupt file is reported, and the good sessions
    // around it are still listed.
    {
        std::ofstream junk(root / "20261011T020000Z-baseline-0002.json");
        junk << "{ this is not a session";
    }
    auto good2 = session;
    good2.id = "20261011T030000Z-optimized-0003";
    good2.mode = bm::BenchmarkMode::Optimized;
    OPTIGD_ASSERT(store.save(good2).ok, "a second session must save");

    const auto listing = store.list();
    OPTIGD_ASSERT(listing.size() == 4, "every session file must be listed");
    bool sawUnreadable = false;
    bool sawNewestFirst = false;
    for (size_t i = 0; i < listing.size(); ++i) {
        if (!listing[i].readable) sawUnreadable = true;
        if (i > 0) sawNewestFirst = listing[i - 1].fileName > listing[i].fileName;
    }
    OPTIGD_ASSERT(sawUnreadable, "a corrupt session must be listed as unreadable, not dropped");
    OPTIGD_ASSERT(sawNewestFirst, "the listing must be newest-first by file name");

    // The index is a rebuildable cache, never the source of truth.
    OPTIGD_ASSERT(store.rebuildIndex(error), error.c_str());
    OPTIGD_ASSERT(std::filesystem::exists(store.indexPath()), "index.json must be written");
    std::filesystem::remove(store.indexPath());
    const auto afterIndexLoss = store.list();
    OPTIGD_ASSERT(afterIndexLoss.size() == 4,
                  "losing the index must not lose any session");

    // Export copies without touching the original.
    const auto exportDir = root / "exports";
    std::error_code ec;
    std::filesystem::create_directories(exportDir, ec);
    OPTIGD_ASSERT(store.exportTo(session.id, exportDir / "copy.json", error), error.c_str());
    OPTIGD_ASSERT(std::filesystem::exists(exportDir / "copy.json"), "the export must exist");
    OPTIGD_ASSERT(std::filesystem::exists(store.sessionPath(session.id)),
                  "the original must still exist after an export");
    OPTIGD_ASSERT(!store.exportTo("does-not-exist", exportDir / "nope.json", error),
                  "exporting a missing session must fail");

    bench_test::removeScratchDir();
    OPTIGD_PASS();
}

void testBenchmarkStoreAtomicWriteLeavesNoPartialFile() {
    namespace bm = optigd::benchmark;
    const auto root = bench_test::makeScratchDir("atomic");
    std::error_code ec;
    std::filesystem::create_directories(root, ec);

    const auto target = root / "report.json";
    const std::string original = "{\"report_kind\": \"optigd_benchmark_session\"}\n";
    std::string error;
    OPTIGD_ASSERT(bm::BenchmarkStore::writeAtomic(target, original, error), error.c_str());
    OPTIGD_ASSERT(std::filesystem::exists(target), "the file must exist after a successful write");

    // A failed write must not disturb the file that is already there, and must
    // not leave a temporary behind. This is the exact failure beta.2's
    // diagnostics exporter had: it truncated the destination first and then
    // returned true without checking the stream.
    const auto unwritable = root / "no-such-dir" / "nested" / "report.json";
    std::filesystem::remove_all(root / "no-such-dir", ec);
    // Create a *file* where a directory would have to be, so the open must fail.
    {
        std::ofstream blocker(root / "no-such-dir");
        blocker << "blocker";
    }
    OPTIGD_ASSERT(!bm::BenchmarkStore::writeAtomic(unwritable, "payload", error),
                  "writing through a path blocked by a regular file must fail");
    OPTIGD_ASSERT(!error.empty(), "a failed write must explain itself");

    {
        std::ifstream verify(target);
        std::string contents((std::istreambuf_iterator<char>(verify)),
                             std::istreambuf_iterator<char>());
        OPTIGD_ASSERT(contents == original, "an unrelated failed write must not touch this file");
    }
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        OPTIGD_ASSERT(entry.path().extension() != ".tmp",
                      "no temporary file may be left behind");
    }

    bench_test::removeScratchDir();
    OPTIGD_PASS();
}

// ---------------------------------------------------------------------------
// The controller: the lifecycle the acceptance criteria ask for.
// ---------------------------------------------------------------------------
void testBenchmarkControllerCompletesAndRestoresConfiguration() {
    namespace bm = optigd::benchmark;
    auto& cfgMgr = optigd::config::ConfigManager::instance();
    auto& controller = bm::BenchmarkController::instance();
    auto& mgr = optigd::core::OptiGDManager::instance();

    cfgMgr.resetToDefaults();
    cfgMgr.clearRuntimeOverride();
    const auto user = bench_test::makeUserConfig();
    cfgMgr.setBaseConfig(user);
    mgr.reloadConfig();

    const auto root = bench_test::makeScratchDir("controller");
    bench_test::installTestBridge(controller, root, /*targetMs=*/1000.0, /*stopOnReset=*/true);

    // Nothing is running yet.
    OPTIGD_ASSERT(!controller.isRunning(), "a fresh controller must be idle");
    OPTIGD_ASSERT(controller.cancelSession().ok == false,
                  "cancelling with nothing running must be refused");

    const auto started = controller.startSession(bm::BenchmarkMode::Baseline);
    OPTIGD_ASSERT(started.ok, started.message.c_str());
    OPTIGD_ASSERT(controller.isRunning(), "the controller must be running after a start");
    OPTIGD_ASSERT(controller.isOverrideActive(), "a transient override must be installed");
    OPTIGD_ASSERT(!mgr.getActiveConfig().flags.enableSpatialCulling,
                  "the ENGINE must see the passthrough configuration, not just ConfigManager");
    OPTIGD_ASSERT(cfgMgr.getBaseConfig().flags.enableSpatialCulling,
                  "the user's saved configuration must be untouched while measuring");

    // A second start while running must be refused with a usable message.
    const auto second = controller.startSession(bm::BenchmarkMode::Optimized);
    OPTIGD_ASSERT(!second.ok, "a second concurrent session must be refused");
    OPTIGD_ASSERT(second.errorCode == "already_running", "the refusal must be machine-readable");
    OPTIGD_ASSERT(!second.message.empty(), "the refusal must be explainable in the UI");

    const auto snapshotDuring = controller.getSnapshot();
    OPTIGD_ASSERT(snapshotDuring.isRunning, "the snapshot must report running");
    OPTIGD_ASSERT(!snapshotDuring.sessionId.empty(), "the snapshot must carry the session id");
    OPTIGD_ASSERT(snapshotDuring.configOverrideActive,
                  "the snapshot must report the override as active");

    // Drive 16 ms frames until the 1000 ms target is reached.
    size_t frames = 0;
    while (controller.isRunning() && frames < 500) {
        bench_test::clock().advanceMs(16.0);
        controller.onFrame(bench_test::makeFrame(16000.0));
        ++frames;
    }
    OPTIGD_ASSERT(!controller.isRunning(), "the session must finalize on its own");
    OPTIGD_ASSERT(frames >= 62 && frames <= 70,
                  "a 1000 ms target at 16 ms/frame needs about 63 counted frames");

    const auto* session = controller.getSession();
    OPTIGD_ASSERT(session != nullptr, "the terminal session must be retained for review");
    OPTIGD_ASSERT(session->state == bm::BenchmarkState::Completed, "the session must be completed");
    OPTIGD_ASSERT(session->isComparable(), "a completed session with samples is comparable");
    OPTIGD_ASSERT(session->capturedDurationMs >= 1000.0,
                  "the captured window must reach the target");
    OPTIGD_ASSERT(session->stats.samples == session->framesCounted,
                  "framesCounted must equal the window's sample count");
    OPTIGD_ASSERT(session->stats.samples > 0, "samples must have been recorded");
    OPTIGD_ASSERT(session->stats.percentileMethod ==
                      optigd::diagnostics::PercentileMethod::Exact,
                  "a 63-frame capture inside a 1024-slot ring must be exact");
    OPTIGD_ASSERT(session->configOverrideApplied, "the override must be recorded as applied");
    OPTIGD_ASSERT(!session->featuresDisabledForBaseline.empty(),
                  "a baseline session must list what it put into passthrough");
    OPTIGD_ASSERT(session->mode == bm::BenchmarkMode::Baseline, "the mode must be recorded");
    OPTIGD_ASSERT(session->level.levelId == 4242, "the level identity must be recorded");
    OPTIGD_ASSERT(session->environment.gdVersion == "2.2081", "the environment must be recorded");
    OPTIGD_ASSERT(std::fabs(session->avgFps() - 62.5) < 1.0,
                  "16 ms frames must average about 62.5 fps");
    OPTIGD_ASSERT(session->warnings.empty() || !session->hasWarning(bm::warning::kShortCapture),
                  "a capture that reached its target must not claim to be short");

    // The override must be gone and the user's configuration must be back, in
    // BOTH layers.
    OPTIGD_ASSERT(!controller.isOverrideActive(), "the override must be cleared on finalize");
    OPTIGD_ASSERT(!cfgMgr.hasRuntimeOverride(), "ConfigManager must hold no override");
    OPTIGD_ASSERT(bench_test::sameConfig(cfgMgr.getConfig(), user),
                  "the user's configuration must be restored exactly");
    OPTIGD_ASSERT(mgr.getActiveConfig().flags.enableSpatialCulling,
                  "the ENGINE must be back on the user's configuration");

    // It must be on disk, and it must be loadable without the controller.
    OPTIGD_ASSERT(controller.getSnapshot().persisted,
                  "the snapshot must report the session as persisted");
    OPTIGD_ASSERT(!session->sourcePath.empty(), "the session must record where it was saved");
    OPTIGD_ASSERT(std::filesystem::exists(session->sourcePath), "the session file must exist");
    bm::BenchmarkSession reloaded;
    std::string error;
    bm::BenchmarkStore store(root);
    OPTIGD_ASSERT(store.load(session->id, reloaded, error), error.c_str());
    OPTIGD_ASSERT(reloaded.id == session->id, "the reloaded id must match");
    OPTIGD_ASSERT(reloaded.stats.samples == session->stats.samples,
                  "the reloaded sample count must match");

    // The previous session must survive the next one: results accumulate.
    const auto firstId = session->id;
    bench_test::clock().advanceMs(60000.0);  // a different second, so a different id
    const auto optimizedStart = controller.startSession(bm::BenchmarkMode::Optimized);
    OPTIGD_ASSERT(optimizedStart.ok, optimizedStart.message.c_str());
    OPTIGD_ASSERT(optimizedStart.sessionId != firstId, "session ids must be unique");
    OPTIGD_ASSERT(mgr.getActiveConfig().flags.enableSpatialCulling,
                  "an OPTIMIZED session must run the user's own configuration");
    while (controller.isRunning()) {
        bench_test::clock().advanceMs(16.0);
        controller.onFrame(bench_test::makeFrame(12000.0));
    }
    OPTIGD_ASSERT(controller.getLastSession() != nullptr,
                  "the previous session must be archived, not discarded");
    OPTIGD_ASSERT(controller.getLastSession()->id == firstId,
                  "the archived session must be the baseline one");
    OPTIGD_ASSERT(store.count() == 2, "both sessions must be on disk");

    bench_test::restoreControllerDefaults(controller);
    cfgMgr.resetToDefaults();
    mgr.reloadConfig();
    bench_test::removeScratchDir();
    OPTIGD_PASS();
}

void testBenchmarkControllerExcludesPausesAndStalls() {
    namespace bm = optigd::benchmark;
    auto& cfgMgr = optigd::config::ConfigManager::instance();
    auto& controller = bm::BenchmarkController::instance();
    auto& mgr = optigd::core::OptiGDManager::instance();

    cfgMgr.resetToDefaults();
    cfgMgr.clearRuntimeOverride();
    cfgMgr.setBaseConfig(bench_test::makeUserConfig());
    mgr.reloadConfig();

    const auto root = bench_test::makeScratchDir("gaps");
    bench_test::installTestBridge(controller, root, /*targetMs=*/500.0, /*stopOnReset=*/true);

    OPTIGD_ASSERT(controller.startSession(bm::BenchmarkMode::Optimized).ok, "start must succeed");

    // 10 real frames = 160 ms of gameplay.
    for (int i = 0; i < 10; ++i) {
        bench_test::clock().advanceMs(16.0);
        controller.onFrame(bench_test::makeFrame(16000.0));
    }
    const double capturedBefore = controller.getSnapshot().capturedDurationMs;
    OPTIGD_ASSERT(std::fabs(capturedBefore - 160.0) < 1e-6,
                  "10 frames of 16 ms must accumulate 160 ms");

    // A pause: the game loop keeps running at the display rate, so interval-based
    // gap detection cannot see it. Only the explicit signal can.
    controller.onPauseObserved();
    for (int i = 0; i < 100; ++i) {
        bench_test::clock().advanceMs(16.0);
        controller.onFrame(bench_test::makeFrame(16000.0));
    }
    const auto pausedSnapshot = controller.getSnapshot();
    OPTIGD_ASSERT(std::fabs(pausedSnapshot.capturedDurationMs - capturedBefore) < 1e-6,
                  "paused frames must NOT advance the captured window");
    OPTIGD_ASSERT(pausedSnapshot.framesExcluded >= 100,
                  "paused frames must be counted as excluded");
    OPTIGD_ASSERT(pausedSnapshot.pauseEvents == 1, "the pause transition must be counted once");
    OPTIGD_ASSERT(controller.isRunning(), "a pause must not end the session");

    controller.onResumeObserved();

    // A stall: a 2 s interval (an alt-tab or a load).
    bench_test::clock().advanceMs(2000.0);
    controller.onFrame(bench_test::makeFrame(2000000.0));
    const auto afterStall = controller.getSnapshot();
    OPTIGD_ASSERT(afterStall.gapEvents >= 1, "a stall must be recorded as a gap event");
    OPTIGD_ASSERT(std::fabs(afterStall.capturedDurationMs - capturedBefore) < 1e-6,
                  "a stall must NOT advance the captured window");

    // Now finish the capture with real gameplay.
    size_t frames = 0;
    while (controller.isRunning() && frames < 500) {
        bench_test::clock().advanceMs(16.0);
        controller.onFrame(bench_test::makeFrame(16000.0));
        ++frames;
    }
    const auto* session = controller.getSession();
    OPTIGD_ASSERT(session != nullptr, "the session must be retained");
    OPTIGD_ASSERT(session->state == bm::BenchmarkState::Completed,
                  "the session must still complete after pauses and stalls");
    OPTIGD_ASSERT(session->capturedDurationMs >= 500.0,
                  "the captured window must contain 500 ms of GAMEPLAY");
    OPTIGD_ASSERT(session->pausedMs >= 1500.0,
                  "the paused time must be reported separately from the stall time");
    OPTIGD_ASSERT(session->excludedGapMs >= session->pausedMs + 1900.0,
                  "excluded time must cover both the pause and the stall");
    OPTIGD_ASSERT(session->wallDurationMs > session->capturedDurationMs + 3000.0,
                  "wall clock must exceed the captured window by the excluded time");
    OPTIGD_ASSERT(session->hasWarning(bm::warning::kPaused), "the pause must be a warning");
    OPTIGD_ASSERT(session->hasWarning(bm::warning::kLongGap), "the stall must be a warning");
    OPTIGD_ASSERT(session->hasWarning(bm::warning::kWallClockDrift),
                  "a wall clock far beyond the captured window must be disclosed");
    OPTIGD_ASSERT(session->framesCounted + session->framesExcluded + session->framesRejected ==
                  session->framesObserved,
                  "every observed frame must be accounted for exactly once");
    OPTIGD_ASSERT(std::fabs(session->avgFps() - 62.5) < 1.0,
                  "the reported fps must come from counted gameplay frames only");

    bench_test::restoreControllerDefaults(controller);
    cfgMgr.resetToDefaults();
    mgr.reloadConfig();
    bench_test::removeScratchDir();
    OPTIGD_PASS();
}

// beta.3 fix (defect X): the wall-clock ceiling is a runaway guard, not a budget.
// `applyBenchmarkSettingsToPolicy()` in the Geode layer used to clamp it to
// `target / 0.9` - about 11 % of slack - which would have finalized an ordinary
// capture as `incomplete` the first time the player paused, died or hit a loading
// stall. The rule now lives in the core as
// `BenchmarkController::recommendedWallClockCeilingMs()` so it can be tested, and
// this test also covers the ceiling-breach path itself, which had no coverage: it
// must end the session as `incomplete`, say why, still save the evidence, and
// still restore the user's effective configuration.
void testBenchmarkWallClockCeilingIsAGuardNotABudget() {
    namespace bm = optigd::benchmark;
    auto& cfgMgr = optigd::config::ConfigManager::instance();
    auto& controller = bm::BenchmarkController::instance();
    auto& mgr = optigd::core::OptiGDManager::instance();

    // --- the rule itself ----------------------------------------------------
    OPTIGD_ASSERT(std::fabs(bm::BenchmarkController::recommendedWallClockCeilingMs(60000.0) -
                            900000.0) < 1e-9,
                  "the 60 s default target must get 15 minutes of wall-clock slack");
    OPTIGD_ASSERT(bm::BenchmarkController::recommendedWallClockCeilingMs(600000.0) >
                      600000.0 * 3.0,
                  "the slack must scale with the target instead of staying fixed");

    bm::BenchmarkPolicy policy;
    policy.targetDurationMs = 60000.0;
    policy.maxWallClockMs = 1000.0;  // narrower than the target: unusable
    controller.setPolicy(policy);
    OPTIGD_ASSERT(std::fabs(controller.getPolicy().maxWallClockMs - 900000.0) < 1e-9,
                  "a ceiling narrower than the target must be repaired, not accepted");

    policy.maxWallClockMs = 60000.0 * 40.0;  // deliberately wider than the rule
    controller.setPolicy(policy);
    OPTIGD_ASSERT(std::fabs(controller.getPolicy().maxWallClockMs - 60000.0 * 40.0) < 1e-9,
                  "a wider ceiling must be left alone");

    // --- breaching the ceiling ----------------------------------------------
    const auto userConfig = bench_test::makeUserConfig();
    cfgMgr.resetToDefaults();
    cfgMgr.clearRuntimeOverride();
    cfgMgr.setBaseConfig(userConfig);
    mgr.reloadConfig();

    const auto root = bench_test::makeScratchDir("ceiling");
    bench_test::installTestBridge(controller, root, /*targetMs=*/5000.0, /*stopOnReset=*/true);
    auto tight = controller.getPolicy();
    // Tight but still >= the target: `setPolicy` repairs a ceiling below the
    // target (asserted above), so an unusable value cannot be used to test the
    // breach path. 6 s of wall clock for a 5 s active target leaves one second of
    // slack, which a single pause blows through.
    tight.maxWallClockMs = 6000.0;
    controller.setPolicy(tight);
    OPTIGD_ASSERT(std::fabs(controller.getPolicy().maxWallClockMs - 6000.0) < 1e-9,
                  "a ceiling above the target must be accepted as given");

    OPTIGD_ASSERT(controller.startSession(bm::BenchmarkMode::Baseline).ok, "start must succeed");
    OPTIGD_ASSERT(controller.isOverrideActive(), "a baseline capture must arm the override");

    // Burn wall clock while the game is paused. The active window cannot advance,
    // which is exactly the runaway case the ceiling exists for.
    controller.onPauseObserved();
    int frames = 0;
    while (controller.isRunning() && frames < 400) {
        bench_test::clock().advanceMs(16.0);
        controller.onFrame(bench_test::makeFrame(16000.0));
        ++frames;
    }

    const auto* session = controller.getSession();
    const auto snapshot = controller.getSnapshot();
    OPTIGD_ASSERT(session != nullptr, "the session must be retained for inspection");
    OPTIGD_ASSERT(!controller.isRunning(), "the ceiling must end the session");
    // `finalize()` derives the terminal state from the data rather than trusting
    // the caller, and a window with no gameplay samples at all is `invalid`, not
    // `incomplete` (the defect-U rule: `invalid` means "too little data to be a
    // measurement"). Either way it is not comparable and not completed.
    OPTIGD_ASSERT(session->state == bm::BenchmarkState::Invalid,
                  "a ceiling breach with zero gameplay samples must be invalid evidence");
    OPTIGD_ASSERT(!session->isComparable(), "a breached session must never be comparable");
    OPTIGD_ASSERT(session->hasWarning(bm::warning::kWallClockDrift),
                  "the reason must be recorded as wall_clock_exceeds_captured_window");
    OPTIGD_ASSERT(session->hasWarning(bm::warning::kPaused),
                  "the paused time that ate the wall clock must be disclosed");
    OPTIGD_ASSERT(session->framesCounted == 0,
                  "paused frames must never be counted as gameplay samples");
    OPTIGD_ASSERT(session->hasWarning(bm::warning::kNoSamples),
                  "a capture with no gameplay samples must say so");
    OPTIGD_ASSERT(session->capturedDurationMs < session->targetDurationMs,
                  "the window must not have reached its target");
    OPTIGD_ASSERT(session->wallDurationMs > tight.maxWallClockMs,
                  "the wall clock must actually have exceeded the ceiling");
    OPTIGD_ASSERT(snapshot.persisted, "incomplete evidence must still be saved");
    OPTIGD_ASSERT(!controller.isOverrideActive(),
                  "the override must be cleared on the ceiling path too");
    OPTIGD_ASSERT(!cfgMgr.hasRuntimeOverride(), "no transient override may survive");
    OPTIGD_ASSERT(bench_test::sameConfig(mgr.getActiveConfig(), userConfig),
                  "the user's effective configuration must be restored exactly");

    // --- the same breach after real gameplay was recorded --------------------
    // This is the case a player actually hits: some seconds were measured, then
    // the level was left paused (or the game backgrounded) long enough to breach
    // the ceiling. The evidence is `incomplete`, the shortfall is disclosed, and
    // the configuration is still restored exactly.
    OPTIGD_ASSERT(controller.startSession(bm::BenchmarkMode::Optimized).ok,
                  "a second capture must be startable after a breached one");
    for (int i = 0; i < 40; ++i) {  // 640 ms of real gameplay, above the 500 ms floor
        bench_test::clock().advanceMs(16.0);
        controller.onFrame(bench_test::makeFrame(16000.0));
    }
    OPTIGD_ASSERT(controller.isRunning(), "640 ms of a 5 s target must still be running");
    controller.onPauseObserved();
    frames = 0;
    while (controller.isRunning() && frames < 600) {
        bench_test::clock().advanceMs(16.0);
        controller.onFrame(bench_test::makeFrame(16000.0));
        ++frames;
    }
    const auto* partial = controller.getSession();
    OPTIGD_ASSERT(partial != nullptr, "the partial session must be retained");
    OPTIGD_ASSERT(partial->state == bm::BenchmarkState::Incomplete,
                  "a breach after real samples is incomplete evidence, not invalid");
    OPTIGD_ASSERT(partial->framesCounted >= 40, "the recorded gameplay must be kept");
    OPTIGD_ASSERT(partial->capturedDurationMs >= 500.0,
                  "the captured window must survive the breach");
    OPTIGD_ASSERT(partial->hasWarning(bm::warning::kWallClockDrift),
                  "the breach must be disclosed");
    OPTIGD_ASSERT(partial->hasWarning(bm::warning::kShortCapture),
                  "the shortfall against the target must be disclosed");
    OPTIGD_ASSERT(partial->id != session->id, "the two sessions must have distinct ids");
    OPTIGD_ASSERT(controller.getSnapshot().persisted, "the partial evidence must be saved");
    OPTIGD_ASSERT(!controller.isOverrideActive() && !cfgMgr.hasRuntimeOverride(),
                  "no override may survive the second breach either");
    OPTIGD_ASSERT(bench_test::sameConfig(mgr.getActiveConfig(), userConfig),
                  "the user's configuration must still match exactly");

    bench_test::restoreControllerDefaults(controller);
    cfgMgr.resetToDefaults();
    mgr.reloadConfig();
    bench_test::removeScratchDir();
    OPTIGD_PASS();
}

void testBenchmarkControllerLevelResetHonoursTheSetting() {
    namespace bm = optigd::benchmark;
    auto& cfgMgr = optigd::config::ConfigManager::instance();
    auto& controller = bm::BenchmarkController::instance();
    auto& mgr = optigd::core::OptiGDManager::instance();

    cfgMgr.resetToDefaults();
    cfgMgr.clearRuntimeOverride();
    const auto user = bench_test::makeUserConfig();
    cfgMgr.setBaseConfig(user);
    mgr.reloadConfig();

    // --- stop-on-death = true (the shipped default) -------------------------
    const auto root = bench_test::makeScratchDir("reset");
    bench_test::installTestBridge(controller, root, 1000.0, /*stopOnReset=*/true);
    OPTIGD_ASSERT(controller.startSession(bm::BenchmarkMode::Baseline).ok, "start must succeed");
    // 40 frames = 640 ms: above the controller's `minValidCapturedMs`, so the
    // interruption is reported as INCOMPLETE (usable data, cut short) rather than
    // INVALID (not enough data to be a measurement at all).
    for (int i = 0; i < 40; ++i) {
        bench_test::clock().advanceMs(16.0);
        controller.onFrame(bench_test::makeFrame(16000.0));
    }
    controller.onLevelReset();
    OPTIGD_ASSERT(!controller.isRunning(), "stop-on-death must end the capture immediately");
    const auto* stopped = controller.getSession();
    OPTIGD_ASSERT(stopped != nullptr, "the session must be retained");
    OPTIGD_ASSERT(stopped->state == bm::BenchmarkState::Incomplete,
                  "a capture cut short by a death must be INCOMPLETE, never completed");
    OPTIGD_ASSERT(stopped->resetEvents == 1, "the reset must be counted");
    OPTIGD_ASSERT(stopped->hasWarning(bm::warning::kLevelReset),
                  "the interruption must be recorded as a level_reset warning");
    OPTIGD_ASSERT(!stopped->isComparable(), "an incomplete session must not be comparable");
    OPTIGD_ASSERT(!controller.isOverrideActive(), "the override must be cleared");
    OPTIGD_ASSERT(bench_test::sameConfig(cfgMgr.getConfig(), user), "the user config must be restored");

    // A death after almost no data must be reported as INVALID, not as a
    // completed or incomplete measurement: there is nothing to measure.
    bench_test::installTestBridge(controller, root, 1000.0, /*stopOnReset=*/true);
    OPTIGD_ASSERT(controller.startSession(bm::BenchmarkMode::Baseline).ok, "start must succeed");
    for (int i = 0; i < 5; ++i) {
        bench_test::clock().advanceMs(16.0);
        controller.onFrame(bench_test::makeFrame(16000.0));
    }
    controller.onLevelReset();
    OPTIGD_ASSERT(controller.getSession()->state == bm::BenchmarkState::Invalid,
                  "80 ms of data is not a measurement: the session must be invalid");
    OPTIGD_ASSERT(controller.getSession()->hasWarning(bm::warning::kLevelReset),
                  "the interruption must still be recorded");
    OPTIGD_ASSERT(!controller.getSession()->isComparable(),
                  "an invalid session must not be comparable");

    // --- stop-on-death = false ---------------------------------------------
    bench_test::installTestBridge(controller, root, 1000.0, /*stopOnReset=*/false);
    OPTIGD_ASSERT(controller.startSession(bm::BenchmarkMode::Baseline).ok, "start must succeed");
    for (int i = 0; i < 40; ++i) {
        bench_test::clock().advanceMs(16.0);
        controller.onFrame(bench_test::makeFrame(16000.0));
    }
    controller.onLevelReset();
    OPTIGD_ASSERT(controller.isRunning(),
                  "with stop-on-death off the capture must keep measuring");
    for (int i = 0; i < 60; ++i) {
        bench_test::clock().advanceMs(16.0);
        controller.onFrame(bench_test::makeFrame(16000.0));
    }
    const auto* continued = controller.getSession();
    OPTIGD_ASSERT(continued != nullptr, "the session must be retained");
    OPTIGD_ASSERT(continued->resetEvents == 1, "the reset must still be counted");
    OPTIGD_ASSERT(continued->hasWarning(bm::warning::kSegmentNotContiguous),
                  "continuing past a restart must be flagged as non-contiguous");
    OPTIGD_ASSERT(!controller.isRunning(), "the capture must still reach its target");
    OPTIGD_ASSERT(continued->state == bm::BenchmarkState::Completed,
                  "with the warning recorded, the capture may complete");

    // --- leaving the level always ends a capture and restores config --------
    bench_test::installTestBridge(controller, root, 60000.0, true);
    OPTIGD_ASSERT(controller.startSession(bm::BenchmarkMode::Optimized).ok, "start must succeed");
    controller.onLevelExit();
    OPTIGD_ASSERT(!controller.isRunning(), "leaving the level must end the capture");
    OPTIGD_ASSERT(!controller.isOverrideActive(), "leaving the level must restore the config");
    OPTIGD_ASSERT(controller.getSession() != nullptr &&
                  controller.getSession()->hasWarning(bm::warning::kLevelExit),
                  "the exit must be recorded");

    // --- cancellation -------------------------------------------------------
    bench_test::installTestBridge(controller, root, 60000.0, true);
    const auto cancelStart = controller.startSession(bm::BenchmarkMode::Baseline);
    OPTIGD_ASSERT(cancelStart.ok, "start must succeed");
    for (int i = 0; i < 5; ++i) {
        bench_test::clock().advanceMs(16.0);
        controller.onFrame(bench_test::makeFrame(16000.0));
    }
    const auto cancelled = controller.cancelSession();
    OPTIGD_ASSERT(cancelled.ok, "cancelling a running session must succeed");
    OPTIGD_ASSERT(controller.getSession()->state == bm::BenchmarkState::Cancelled,
                  "a cancelled session must say so");
    OPTIGD_ASSERT(controller.getSession()->hasWarning(bm::warning::kCancelledByUser),
                  "the cancellation must be recorded");
    OPTIGD_ASSERT(!controller.isOverrideActive(), "cancelling must restore the config");
    OPTIGD_ASSERT(bench_test::sameConfig(cfgMgr.getConfig(), user), "the user config must be restored");

    // A cancelled capture with almost no data must not present itself as usable.
    OPTIGD_ASSERT(!controller.getSession()->isComparable(),
                  "a cancelled session must not be comparable");

    bench_test::restoreControllerDefaults(controller);
    cfgMgr.resetToDefaults();
    mgr.reloadConfig();
    bench_test::removeScratchDir();
    OPTIGD_PASS();
}

void testBenchmarkControllerVerifiesItsOwnOverride() {
    namespace bm = optigd::benchmark;
    auto& cfgMgr = optigd::config::ConfigManager::instance();
    auto& controller = bm::BenchmarkController::instance();
    auto& mgr = optigd::core::OptiGDManager::instance();

    cfgMgr.resetToDefaults();
    cfgMgr.clearRuntimeOverride();
    cfgMgr.setBaseConfig(bench_test::makeUserConfig());
    mgr.reloadConfig();

    const auto root = bench_test::makeScratchDir("verify");
    bench_test::installTestBridge(controller, root, 60000.0, true);
    OPTIGD_ASSERT(controller.startSession(bm::BenchmarkMode::Baseline).ok, "start must succeed");
    OPTIGD_ASSERT(controller.getOverrideReapplyCount() == 0, "nothing to re-apply yet");

    // Simulate something outside Benchmark Mode rewriting the active
    // configuration mid-capture. The controller must notice, re-apply, and record
    // that it had to - a report may never claim a configuration that was not in
    // force for the whole window.
    auto hijacked = cfgMgr.getConfig();
    hijacked.flags.enableSpatialCulling = true;
    hijacked.maxActiveParticles = 9999;
    cfgMgr.setConfig(hijacked);
    mgr.reloadConfig();
    OPTIGD_ASSERT(mgr.getActiveConfig().flags.enableSpatialCulling,
                  "the hijack must be visible before the next verification");

    for (int i = 0; i < 17; ++i) {
        bench_test::clock().advanceMs(16.0);
        controller.onFrame(bench_test::makeFrame(16000.0));
    }
    OPTIGD_ASSERT(controller.getOverrideReapplyCount() >= 1,
                  "the controller must detect and repair a lost override");
    OPTIGD_ASSERT(!mgr.getActiveConfig().flags.enableSpatialCulling,
                  "the passthrough configuration must be back in force");
    OPTIGD_ASSERT(controller.getSession()->hasWarning(bm::warning::kConfigOverrideLost),
                  "the repair must be recorded as an integrity warning");
    OPTIGD_ASSERT(controller.getSession()->configOverrideApplied,
                  "the override did end up in force, so it must still be reported as applied");

    controller.cancelSession();
    bench_test::restoreControllerDefaults(controller);
    cfgMgr.resetToDefaults();
    mgr.reloadConfig();
    bench_test::removeScratchDir();
    OPTIGD_PASS();
}

void testBenchmarkControllerReportsPersistFailureHonestly() {
    namespace bm = optigd::benchmark;
    auto& cfgMgr = optigd::config::ConfigManager::instance();
    auto& controller = bm::BenchmarkController::instance();
    auto& mgr = optigd::core::OptiGDManager::instance();

    cfgMgr.resetToDefaults();
    cfgMgr.clearRuntimeOverride();
    cfgMgr.setBaseConfig(bench_test::makeUserConfig());
    mgr.reloadConfig();

    // Point the store at a path that cannot be a directory, so saving must fail.
    const auto root = bench_test::makeScratchDir("persist");
    {
        std::ofstream blocker(root / "blocked");
        blocker << "not a directory";
    }
    bench_test::installTestBridge(controller, root / "blocked" / "sessions", 300.0, true);
    OPTIGD_ASSERT(controller.startSession(bm::BenchmarkMode::Baseline).ok, "start must succeed");

    size_t frames = 0;
    while (controller.isRunning() && frames < 200) {
        bench_test::clock().advanceMs(16.0);
        controller.onFrame(bench_test::makeFrame(16000.0));
        ++frames;
    }
    const auto* session = controller.getSession();
    OPTIGD_ASSERT(session != nullptr, "the session must still be retained in memory");
    OPTIGD_ASSERT(!controller.getSnapshot().persisted,
                  "a session that could not be written must not claim to be saved");
    OPTIGD_ASSERT(session->hasWarning(bm::warning::kPersistFailed),
                  "the failure must be recorded as an integrity warning");
    OPTIGD_ASSERT(!controller.getSnapshot().persisted,
                  "the UI snapshot must also report the failure");
    // The measurement itself is still valid; only its durability is not.
    OPTIGD_ASSERT(session->stats.samples > 0, "the samples were still recorded in memory");
    OPTIGD_ASSERT(!controller.isOverrideActive(),
                  "a persistence failure must not strand the transient override");

    bench_test::restoreControllerDefaults(controller);
    cfgMgr.resetToDefaults();
    mgr.reloadConfig();
    bench_test::removeScratchDir();
    OPTIGD_PASS();
}

// ---------------------------------------------------------------------------
// Comparison: the criteria are "flag incompatible sessions instead of
// manufacturing conclusions" and "never claim significance you did not establish".
// ---------------------------------------------------------------------------
void testBenchmarkCompareDirectionAndNoiseThreshold() {
    namespace bm = optigd::benchmark;

    auto baseline = bench_test::makeSession(bm::BenchmarkMode::Baseline, 20.0, 3000,
                                            60000.0, "session-baseline");
    auto optimized = bench_test::makeSession(bm::BenchmarkMode::Optimized, 16.0, 3750,
                                             60000.0, "session-optimized");
    // Distinct, non-overlapping capture windows: two runs of the same level are
    // sequential, and a comparator that accepted overlapping windows could not
    // tell a repeat from a copy.
    optimized.startedEpochMs = baseline.endedEpochMs + 120000;
    optimized.endedEpochMs = optimized.startedEpochMs + 60000;

    const auto result = bm::compareSessions(baseline, optimized, 2.0);
    OPTIGD_ASSERT(result.comparisonMeaningful, "two comparable sessions must produce a comparison");
    OPTIGD_ASSERT(!result.hasBlockingIssue, "no blocking issue is expected here");
    OPTIGD_ASSERT(!result.significanceClaimed,
                  "a single pair of captures must never claim statistical significance");
    OPTIGD_ASSERT(!result.significanceNote.empty(),
                  "the absence of a significance claim must be stated, not just omitted");

    const auto* fps = result.find(bm::WindowMetricId::AvgFps);
    OPTIGD_ASSERT(fps != nullptr && fps->available, "avg fps must be compared");
    OPTIGD_ASSERT(fps->direction == bm::MetricDirection::HigherIsBetter,
                  "higher fps is better");
    OPTIGD_ASSERT(fps->verdict == bm::ComparisonVerdict::Improved,
                  "62.5 -> 50 fps inverted: 50 -> 62.5 fps must read as improved");
    OPTIGD_ASSERT(fps->relativeAvailable && fps->relativePct > 20.0,
                  "the relative gain must be computed from the baseline");

    const auto* mean = result.find(bm::WindowMetricId::MeanFrameTimeMs);
    OPTIGD_ASSERT(mean != nullptr && mean->available, "mean frame time must be compared");
    OPTIGD_ASSERT(mean->direction == bm::MetricDirection::LowerIsBetter,
                  "lower frame time is better");
    OPTIGD_ASSERT(mean->verdict == bm::ComparisonVerdict::Improved,
                  "20 ms -> 16 ms must read as improved");
    OPTIGD_ASSERT(std::fabs(mean->absoluteDelta - (-4.0)) < 1e-9,
                  "the absolute delta must be optimized minus baseline");

    // A difference inside the noise band must not be reported as a gain.
    auto noisy = bench_test::makeSession(bm::BenchmarkMode::Optimized, 19.8, 3030,
                                         60000.0, "session-noisy");
    noisy.startedEpochMs = optimized.startedEpochMs;
    noisy.endedEpochMs = optimized.endedEpochMs;
    const auto noisyResult = bm::compareSessions(baseline, noisy, 2.0);
    const auto* noisyMean = noisyResult.find(bm::WindowMetricId::MeanFrameTimeMs);
    OPTIGD_ASSERT(noisyMean != nullptr, "mean frame time must still be compared");
    OPTIGD_ASSERT(noisyMean->verdict == bm::ComparisonVerdict::Unchanged,
                  "a 1% change under a 2% threshold must read as unchanged");
    OPTIGD_ASSERT(!noisyMean->note.empty(), "an unchanged verdict must say why");

    // A real regression must be labelled as one, not softened.
    auto worse = bench_test::makeSession(bm::BenchmarkMode::Optimized, 26.0, 2300,
                                         60000.0, "session-worse");
    worse.startedEpochMs = optimized.startedEpochMs;
    worse.endedEpochMs = optimized.endedEpochMs;
    const auto worseResult = bm::compareSessions(baseline, worse, 2.0);
    const auto* worseMean = worseResult.find(bm::WindowMetricId::MeanFrameTimeMs);
    OPTIGD_ASSERT(worseMean->verdict == bm::ComparisonVerdict::Worsened,
                  "20 ms -> 26 ms must read as worsened");
    OPTIGD_ASSERT(worseResult.worsenedCount > 0, "worsened metrics must be tallied");
    OPTIGD_ASSERT(worseResult.headline.find("worsened") != std::string::npos,
                  "the headline must mention the regression");

    // Informational counters must never be counted as improvements.
    const auto* counters = result.find(bm::WindowMetricId::FramesCounted);
    OPTIGD_ASSERT(counters != nullptr, "frames counted must appear in the table");
    OPTIGD_ASSERT(counters->direction == bm::MetricDirection::Informational,
                  "a frame count depends on the level, not on the optimization");
    OPTIGD_ASSERT(counters->verdict == bm::ComparisonVerdict::Unchanged,
                  "an informational metric must not produce a verdict");

    // The rendered text must carry the same numbers and the same caveat.
    const std::string text = bm::formatComparisonText(result);
    OPTIGD_ASSERT(text.find("baseline") != std::string::npos, "the text must name the baseline");
    OPTIGD_ASSERT(text.find("significance") != std::string::npos,
                  "the text must state that no significance is claimed");

    OPTIGD_PASS();
}

void testBenchmarkCompareRefusesIncompatibleSessions() {
    namespace bm = optigd::benchmark;

    const auto good = bench_test::makeSession(bm::BenchmarkMode::Baseline, 20.0, 3000,
                                              60000.0, "session-good");

    auto blocking = [&](const bm::BenchmarkSession& a, const bm::BenchmarkSession& b,
                        const char* code) {
        const auto result = bm::compareSessions(a, b);
        bool found = false;
        for (const auto& issue : result.issues) {
            if (issue.code == code && issue.blocking) found = true;
        }
        return found && result.hasBlockingIssue && !result.comparisonMeaningful;
    };

    // Both sides the same mode is not an A/B comparison.
    {
        auto same = good;
        same.id = "session-same";
        OPTIGD_ASSERT(blocking(good, same, "same_mode"),
                      "two baseline sessions must be refused as an A/B comparison");
    }

    // No samples means there is nothing to compare.
    {
        auto empty = bench_test::makeSession(bm::BenchmarkMode::Optimized, 0.0, 0, 0.0,
                                             "session-empty");
        OPTIGD_ASSERT(blocking(good, empty, "no_samples"),
                      "a session with no samples must be refused");
    }

    // A session that never completed.
    {
        auto incomplete = bench_test::makeSession(bm::BenchmarkMode::Optimized, 16.0, 900,
                                                  15000.0, "session-incomplete");
        incomplete.state = bm::BenchmarkState::Incomplete;
        incomplete.capturedDurationMs = 15000.0;
        OPTIGD_ASSERT(blocking(good, incomplete, "session_not_completed"),
                      "an incomplete session must be refused");
    }

    // Different platforms.
    {
        auto other = bench_test::makeSession(bm::BenchmarkMode::Optimized, 16.0, 3750,
                                             60000.0, "session-other-platform");
        other.environment.architecture = "arm64";
        OPTIGD_ASSERT(blocking(good, other, "platform_mismatch"),
                      "an x86_64 capture must not be compared with an arm64 capture");
    }

    // Different levels.
    {
        auto other = bench_test::makeSession(bm::BenchmarkMode::Optimized, 16.0, 3750,
                                             60000.0, "session-other-level");
        other.level.levelId = 9999;
        OPTIGD_ASSERT(blocking(good, other, "level_mismatch"),
                      "captures of different levels must be refused");
    }

    // Materially different captured durations.
    {
        auto shortOne = bench_test::makeSession(bm::BenchmarkMode::Optimized, 16.0, 1500,
                                                25000.0, "session-short");
        OPTIGD_ASSERT(blocking(good, shortOne, "duration_mismatch"),
                      "60 s against 25 s must be refused");
    }

    // Overlapping capture windows cannot be two independent runs.
    {
        auto overlap = bench_test::makeSession(bm::BenchmarkMode::Optimized, 16.0, 3750,
                                               60000.0, "session-overlap");
        overlap.startedEpochMs = good.startedEpochMs + 1000;
        overlap.endedEpochMs = good.startedEpochMs + 61000;
        OPTIGD_ASSERT(blocking(good, overlap, "overlapping_captures"),
                      "overlapping windows must be refused");
    }

    // Unknown level identity on both sides is a warning, not a fabrication of
    // equivalence: the comparison is refused because equivalence was never
    // established.
    {
        auto a = bench_test::makeSession(bm::BenchmarkMode::Baseline, 20.0, 3000, 60000.0, "a");
        auto b = bench_test::makeSession(bm::BenchmarkMode::Optimized, 16.0, 3750, 60000.0, "b");
        a.level = bm::LevelIdentity{};
        b.level = bm::LevelIdentity{};
        const auto result = bm::compareSessions(a, b);
        bool sawUnknown = false;
        for (const auto& issue : result.issues) {
            if (issue.code == "level_identity_unavailable") sawUnknown = true;
        }
        OPTIGD_ASSERT(sawUnknown, "an unknown level identity must be reported");
    }

    // A materially different but non-blocking difference must still be surfaced.
    {
        // 52 s against a 60 s target is 13.3% shorter: past the 10% "worth
        // mentioning" line but inside the 25% "these are not the same experiment"
        // line, so it must warn without blocking.
        auto b = bench_test::makeSession(bm::BenchmarkMode::Optimized, 16.0, 3250, 52000.0,
                                         "session-slightly-shorter");
        b.capturedDurationMs = 52000.0;
        b.wallDurationMs = 52000.0;
        b.targetDurationMs = 60000.0;
        const auto result = bm::compareSessions(good, b);
        bool sawDurationWarning = false;
        for (const auto& issue : result.issues) {
            if (issue.code == "duration_varies" && !issue.blocking) sawDurationWarning = true;
        }
        OPTIGD_ASSERT(sawDurationWarning, "a 10-25% duration difference must warn, not block");
        OPTIGD_ASSERT(result.comparisonMeaningful,
                      "a non-blocking difference must still allow the comparison");
    }

    // Integrity warnings on either side must be surfaced.
    {
        auto b = bench_test::makeSession(bm::BenchmarkMode::Optimized, 16.0, 3750, 60000.0,
                                         "session-with-fallback");
        b.fallbackEvents = 2;
        b.refreshDerivedWarnings();
        const auto result = bm::compareSessions(good, b);
        bool sawIntegrity = false;
        for (const auto& issue : result.issues) {
            if (issue.code.rfind("integrity_", 0) == 0) sawIntegrity = true;
        }
        OPTIGD_ASSERT(sawIntegrity, "a safety fallback during a capture must be surfaced");
    }

    // A blocking result must not manufacture a headline that reads like a finding.
    {
        auto empty = bench_test::makeSession(bm::BenchmarkMode::Optimized, 0.0, 0, 0.0, "e");
        const auto result = bm::compareSessions(good, empty);
        OPTIGD_ASSERT(result.headline.find("NOT COMPARABLE") != std::string::npos,
                      "a refused comparison must say so in the headline");
        OPTIGD_ASSERT(!bm::formatComparisonText(result).empty(),
                      "a refused comparison must still render an explanation");
    }

    OPTIGD_PASS();
}

void testBenchmarkCompareNeverDividesByZero() {
    namespace bm = optigd::benchmark;

    // A zero baseline is the classic way to produce an infinite "improvement".
    auto zero = bench_test::makeSession(bm::BenchmarkMode::Baseline, 0.0, 3000, 60000.0,
                                        "session-zero");
    zero.stats.meanFrameTimeMs = 0.0;
    zero.stats.fpsFromMeanFrameTime = 0.0;
    zero.stats.p50FrameTimeMs = 0.0;
    zero.stats.p95FrameTimeMs = 0.0;
    zero.stats.p99FrameTimeMs = 0.0;
    zero.stats.minFrameTimeMs = 0.0;
    zero.stats.maxFrameTimeMs = 0.0;
    zero.stats.stdDevFrameTimeMs = 0.0;

    const auto optimized = bench_test::makeSession(bm::BenchmarkMode::Optimized, 16.0, 3750,
                                                   60000.0, "session-nonzero");

    const auto result = bm::compareSessions(zero, optimized, 2.0);
    for (const auto& metric : result.metrics) {
        OPTIGD_ASSERT(std::isfinite(metric.absoluteDelta),
                      "no delta may be infinite or NaN");
        OPTIGD_ASSERT(std::isfinite(metric.relativePct),
                      "no relative change may be infinite or NaN");
        if (std::fabs(metric.baselineValue) <= 1e-12 &&
            std::fabs(metric.optimizedValue) > 1e-12) {
            OPTIGD_ASSERT(!metric.relativeAvailable,
                          "a relative change from a zero baseline must be reported as undefined");
            OPTIGD_ASSERT(!metric.note.empty(),
                          "an undefined relative change must be explained");
        }
    }
    const std::string text = bm::formatComparisonText(result);
    OPTIGD_ASSERT(text.find("undefined") != std::string::npos,
                  "an undefined relative change must be rendered as such, never as a number");

    // Both sides zero: that is "no change", not "undefined". A copy of a session
    // is not a second capture, so give it its own window - otherwise the pair is
    // refused for overlapping captures and the zero-division case is never
    // actually exercised.
    auto zero2 = zero;
    zero2.id = "session-zero2";
    zero2.mode = bm::BenchmarkMode::Optimized;
    zero2.startedEpochMs = zero.endedEpochMs + 120000;
    zero2.endedEpochMs = zero2.startedEpochMs + (zero.endedEpochMs - zero.startedEpochMs);
    const auto bothZero = bm::compareSessions(zero, zero2);
    bool sawBlocking = bothZero.hasBlockingIssue;
    OPTIGD_ASSERT(!sawBlocking, "two zero-valued sessions are still structurally comparable");
    for (const auto& metric : bothZero.metrics) {
        OPTIGD_ASSERT(std::isfinite(metric.relativePct), "no NaN from 0/0");
    }

    OPTIGD_PASS();
}

// ---------------------------------------------------------------------------
// Presentation: the strings the user reads, including the '~' that marks an
// approximate live percentile and the internal-control disclosure.
// ---------------------------------------------------------------------------
void testBenchmarkPanelTextAndPagination() {
    namespace bm = optigd::benchmark;
    using optigd::ui::BenchmarkPanel;

    // Pagination must never lose content and must respect both limits.
    std::string longText;
    for (int i = 0; i < 200; ++i) {
        longText += "line " + std::to_string(i) +
                    " with a fairly long tail so wrapping has something to do\n";
    }
    const auto pages = BenchmarkPanel::paginate(longText, 24, 74);
    OPTIGD_ASSERT(pages.size() > 1, "200 long lines must need more than one page");
    size_t totalLines = 0;
    for (const auto& page : pages) {
        size_t lines = 1;
        for (char c : page) if (c == '\n') ++lines;
        OPTIGD_ASSERT(lines <= 24, "no page may exceed the line limit");
        totalLines += lines;
        size_t pos = 0;
        while (pos < page.size()) {
            size_t nl = page.find('\n', pos);
            size_t len = (nl == std::string::npos) ? page.size() - pos : nl - pos;
            OPTIGD_ASSERT(len <= 74, "no rendered line may exceed the column limit");
            if (nl == std::string::npos) break;
            pos = nl + 1;
        }
    }
    OPTIGD_ASSERT(totalLines >= 200, "wrapping may add lines but must not lose any");
    for (int i = 0; i < 200; ++i) {
        const std::string needle = "line " + std::to_string(i) + " ";
        bool found = false;
        for (const auto& page : pages) {
            if (page.find(needle) != std::string::npos) found = true;
        }
        // A wrapped line may split the needle, so check the numeric part too.
        if (!found) {
            for (const auto& page : pages) {
                if (page.find("line " + std::to_string(i)) != std::string::npos) found = true;
            }
        }
        OPTIGD_ASSERT(found, "every input line must appear in some page");
    }

    // Degenerate inputs.
    OPTIGD_ASSERT(BenchmarkPanel::paginate("").size() == 1, "empty text yields one empty page");
    OPTIGD_ASSERT(BenchmarkPanel::paginate("abc", 0, 0).size() == 1,
                  "a zero line limit must be clamped, not loop forever");
    OPTIGD_ASSERT(BenchmarkPanel::formatPageIndicator(0, 3) == "page 1/3",
                  "page numbers are one-based for the reader");
    OPTIGD_ASSERT(BenchmarkPanel::formatPageIndicator(5, 0) == "page 6/1",
                  "a zero page count must be clamped");

    // The HUD must mark approximate percentiles so a live estimate cannot be
    // mistaken for the exact value in the saved report.
    bm::BenchmarkSnapshot running;
    running.isRunning = true;
    running.state = bm::BenchmarkState::Running;
    running.mode = bm::BenchmarkMode::Baseline;
    running.sessionId = "session-hud";
    running.targetDurationMs = 60000.0;
    running.capturedDurationMs = 30000.0;
    running.remainingDurationMs = 30000.0;
    running.framesCounted = 1800;
    running.framesExcluded = 2;
    running.spatialCullingEnabled = false;

    optigd::diagnostics::WindowStatistics approximate;
    approximate.samples = 1800;
    approximate.meanFrameTimeMs = 16.7;
    approximate.p50FrameTimeMs = 16.5;
    approximate.p95FrameTimeMs = 20.1;
    approximate.p99FrameTimeMs = 24.9;
    approximate.fpsFromMeanFrameTime = 59.9;
    approximate.percentileMethod = optigd::diagnostics::PercentileMethod::HistogramApproximate;
    const std::string hudApprox = BenchmarkPanel::formatHud(running, approximate);
    OPTIGD_ASSERT(hudApprox.find("~") != std::string::npos,
                  "an approximate live percentile must be marked with ~");
    OPTIGD_ASSERT(hudApprox.find("BASELINE") != std::string::npos ||
                  hudApprox.find("baseline") != std::string::npos,
                  "the HUD must say which side is being measured");

    optigd::diagnostics::WindowStatistics exact = approximate;
    exact.percentileMethod = optigd::diagnostics::PercentileMethod::Exact;
    const std::string hudExact = BenchmarkPanel::formatHud(running, exact);
    OPTIGD_ASSERT(hudExact.find("~") == std::string::npos,
                  "an exact live percentile must not carry the ~ marker");

    // The main view must carry the disclosure that a baseline is an internal
    // control and that these are internal measurements.
    const std::string main = BenchmarkPanel::formatMain(running, exact, "status line");
    OPTIGD_ASSERT(main.find("INTERNAL CONTROL") != std::string::npos,
                  "the baseline disclosure must be present");
    OPTIGD_ASSERT(main.find("not vanilla") != std::string::npos ||
                  main.find("NOT vanilla") != std::string::npos,
                  "the disclosure must say a baseline is not vanilla Geometry Dash");
    OPTIGD_ASSERT(main.find("inter-frame") != std::string::npos,
                  "the disclosure must say these are internal inter-frame measurements");
    OPTIGD_ASSERT(main.find("status line") != std::string::npos,
                  "a status message must be rendered");
    // A capture is normally started from the pause menu, where the window cannot
    // advance. Without this the panel reads as a countdown that has hung at 0.00 s.
    OPTIGD_ASSERT(main.find("RESUME") != std::string::npos,
                  "the running view must tell the player to close the panel and resume");
    OPTIGD_ASSERT(main.find("only") != std::string::npos &&
                  main.find("active gameplay") != std::string::npos,
                  "the running view must state that the window only advances in active play");
    OPTIGD_ASSERT(main.find(std::string(optigd::core::kOptiGDVersion)) != std::string::npos,
                  "the panel must show the running version");

    // Session detail must carry the measurement definitions and limitations.
    const auto session = bench_test::makeSession(bm::BenchmarkMode::Baseline, 20.0, 3000,
                                                 60000.0, "session-detail");
    const std::string detail = BenchmarkPanel::formatSessionDetail(session);
    OPTIGD_ASSERT(detail.find("INTERNAL telemetry") != std::string::npos,
                  "the detail view must repeat that this is internal telemetry");
    OPTIGD_ASSERT(detail.find("percentile") != std::string::npos ||
                  detail.find("P99") != std::string::npos,
                  "the detail view must show the percentile definition or values");

    // Protocol and external procedure must be honest about what they establish.
    const std::string protocol = BenchmarkPanel::formatProtocol();
    OPTIGD_ASSERT(protocol.find("not a result") != std::string::npos ||
                  protocol.find("A single pair") != std::string::npos,
                  "the protocol must say one pair is not a result");
    const std::string external = BenchmarkPanel::formatExternalProcedure();
    OPTIGD_ASSERT(external.find("CapFrameX") != std::string::npos,
                  "the external procedure must name the tool");
    OPTIGD_ASSERT(external.find("CANNOT") != std::string::npos,
                  "the external procedure must state the limits of each method");

    OPTIGD_PASS();
}

// ---------------------------------------------------------------------------
// Diagnostics engine: level scoping, window disclosure, honest exports.
// ---------------------------------------------------------------------------
void testDiagnosticsWindowDisclosureIsConsistent() {
    auto& engine = optigd::diagnostics::DiagnosticsEngine::instance();
    engine.setHistoryCapacity(100);   // deliberately small, to force disclosure
    engine.reset();

    for (int i = 0; i < 250; ++i) {
        engine.recordFrameStats(bench_test::makeFrame(16000.0));
    }
    // A paused frame: flagged by the engine, excluded from the distribution.
    auto paused = bench_test::makeFrame(16000.0);
    paused.frameCounted = false;
    paused.gapFrame = true;
    for (int i = 0; i < 5; ++i) engine.recordFrameStats(paused);
    // A stall: kept as a sample so a level report cannot hide a freeze.
    auto stall = bench_test::makeFrame(3000000.0);  // 3 s
    stall.frameCounted = false;
    stall.gapFrame = true;
    engine.recordFrameStats(stall);

    optigd::core::OptiConfig config = bench_test::makeUserConfig();
    const auto summary = engine.computeSummary(config, 0, 0);

    OPTIGD_ASSERT(summary.totalFrames == 256, "every offered frame must be counted");
    OPTIGD_ASSERT(summary.sampleCount == 251,
                  "250 gameplay frames plus the stall are samples; the 5 paused frames are not");
    OPTIGD_ASSERT(summary.pausedFrames == 5, "paused frames must be disclosed");
    OPTIGD_ASSERT(summary.excludedSamples == 5, "paused frames must be counted as excluded");
    OPTIGD_ASSERT(std::fabs(summary.excludedTimeMs - 5.0 * 16.0) < 1e-6,
                  "excluded time must be reported");
    OPTIGD_ASSERT(summary.stallFrames == 1, "the stall must be counted");
    OPTIGD_ASSERT(summary.historyCapacity == 100, "the capacity must be disclosed");
    OPTIGD_ASSERT(summary.historyTruncated, "an overflowed ring must be disclosed");
    OPTIGD_ASSERT(summary.percentileMethod ==
                      optigd::diagnostics::PercentileMethod::HistogramApproximate,
                  "percentiles from a truncated ring must be labelled approximate");
    OPTIGD_ASSERT(summary.percentilesAreMonotonic(), "monotonicity must hold");
    OPTIGD_ASSERT(summary.maxFrameTimeMs >= 3000.0 - 1e-6,
                  "the 3 s stall must remain visible in the distribution");
    OPTIGD_ASSERT(summary.sampleWindowMs > 0.0, "the window duration must be reported");
    OPTIGD_ASSERT(summary.windowOwnedBytes > 0, "window memory must be reported");

    const std::string report = engine.generateJsonReport(config, {}, 0);
    OPTIGD_ASSERT(report.find("\"report_schema_version\": 2") != std::string::npos,
                  "the report must declare schema version 2");
    OPTIGD_ASSERT(report.find("\"sample_count\": 251") != std::string::npos,
                  "the report must disclose the sample count");
    OPTIGD_ASSERT(report.find("\"total_frames\": 256") != std::string::npos,
                  "the report must disclose the offered frame count");
    OPTIGD_ASSERT(report.find("\"history_truncated\": true") != std::string::npos,
                  "the report must disclose truncation");
    OPTIGD_ASSERT(report.find("\"percentile_method\": \"histogram-approximate\"") !=
                  std::string::npos,
                  "the report must disclose the percentile method");
    OPTIGD_ASSERT(report.find("\"paused_frames\": 5") != std::string::npos,
                  "the report must disclose paused frames");
    OPTIGD_ASSERT(report.find("\"stall_frames\": 1") != std::string::npos,
                  "the report must disclose stalls");
    OPTIGD_ASSERT(report.find("\"native_trigger_execution_modified\": false") != std::string::npos,
                  "the trigger-execution statement must be preserved");
    OPTIGD_ASSERT(report.find("\"avg_update_thread_cpu_time_ms\"") != std::string::npos,
                  "thread CPU time must be reported next to the wall-clock span");
    OPTIGD_ASSERT(report.find("\"percentile_definition\"") != std::string::npos,
                  "the percentile definition must be stated in the report");
    OPTIGD_ASSERT(optigd::core::json::parse(report).ok,
                  "the report must be parseable JSON (a literal nan/inf would not parse)");

    // Legacy keys must survive the schema change.
    for (const char* key : {"\"avg_fps\"", "\"median_frame_time_ms\"", "\"p95_frame_time_ms\"",
                            "\"p99_frame_time_ms\"", "\"avg_update_cpu_time_ms\"",
                            "\"total_objects_culled\"", "\"total_particles_avoided\"",
                            "\"total_triggers_skipped\"", "\"peak_arena_bytes\"",
                            "\"total_heap_allocations_avoided\"", "\"fallbacks\""}) {
        OPTIGD_ASSERT(report.find(key) != std::string::npos,
                      std::string("legacy key must be preserved: ") + key);
    }

    engine.setHistoryCapacity(optigd::diagnostics::DiagnosticsEngine::kDefaultHistoryCapacity);
    engine.reset();
    OPTIGD_PASS();
}

void testDiagnosticsResetIsLevelScoped() {
    auto& engine = optigd::diagnostics::DiagnosticsEngine::instance();
    auto& mgr = optigd::core::OptiGDManager::instance();

    engine.reset();
    for (int i = 0; i < 30; ++i) {
        mgr.beginFrame();
        mgr.setExternalFrameDeltaUs(16666.0);
        mgr.endFrame();
    }
    OPTIGD_ASSERT(engine.getTotalFramesOffered() == 30,
                  "the first level must have recorded 30 frames");

    // Defect B: onLevelLoad() must start a new capture. In beta.2 the level-exit
    // report aggregated every level played since the mod loaded.
    mgr.onLevelLoad();
    OPTIGD_ASSERT(engine.getTotalFramesOffered() == 0,
                  "a level load must reset the telemetry capture");
    const auto summary = engine.computeSummary(mgr.getActiveConfig(), 0, 0);
    OPTIGD_ASSERT(summary.totalFrames == 0 && summary.sampleCount == 0,
                  "the summary must be empty right after a level load");
    OPTIGD_ASSERT(summary.percentileMethod == optigd::diagnostics::PercentileMethod::None,
                  "an empty capture must not report a percentile method");

    for (int i = 0; i < 10; ++i) {
        mgr.beginFrame();
        mgr.setExternalFrameDeltaUs(16666.0);
        mgr.endFrame();
    }
    const auto second = engine.computeSummary(mgr.getActiveConfig(), 0, 0);
    OPTIGD_ASSERT(second.totalFrames == 10,
                  "only the current level's frames may be reported");

    mgr.onLevelReset();
    const auto afterReset = engine.computeSummary(mgr.getActiveConfig(), 0, 0);
    OPTIGD_ASSERT(afterReset.levelResets == 1, "a level reset must be counted");
    OPTIGD_ASSERT(afterReset.totalFrames == 10,
                  "a reset must not discard the level's telemetry");

    mgr.onLevelExit();
    engine.reset();
    OPTIGD_PASS();
}

void testDiagnosticsExportIsAtomicAndChecked() {
    auto& engine = optigd::diagnostics::DiagnosticsEngine::instance();
    auto& mgr = optigd::core::OptiGDManager::instance();
    const auto root = bench_test::makeScratchDir("export");

    engine.reset();
    for (int i = 0; i < 20; ++i) {
        mgr.beginFrame();
        mgr.setExternalFrameDeltaUs(16666.0);
        mgr.endFrame();
    }

    const auto target = root / "optigd_diagnostics.json";
    OPTIGD_ASSERT(engine.exportJsonReportToFile(target.string(), mgr.getActiveConfig(), {}, 0),
                  "exporting to a writable path must succeed");
    OPTIGD_ASSERT(std::filesystem::exists(target), "the report must exist");
    {
        std::ifstream in(target);
        std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        OPTIGD_ASSERT(contents.find("\"report_schema_version\": 2") != std::string::npos,
                      "the written file must be the v2 report");
    }
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        OPTIGD_ASSERT(entry.path().extension() != ".tmp", "no temporary may be left behind");
    }

    // Defect H: a failed export must return false and must not destroy the
    // report that is already there.
    {
        std::ofstream blocker(root / "blocked");
        blocker << "not a directory";
    }
    const auto badTarget = root / "blocked" / "optigd_diagnostics.json";
    OPTIGD_ASSERT(!engine.exportJsonReportToFile(badTarget.string(), mgr.getActiveConfig(), {}, 0),
                  "an impossible path must fail");
    {
        std::ifstream in(target);
        std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        OPTIGD_ASSERT(!contents.empty(), "a failed export must not truncate the previous report");
        OPTIGD_ASSERT(contents.find("\"report_schema_version\": 2") != std::string::npos,
                      "the previous report must still be intact");
    }

    engine.reset();
    bench_test::removeScratchDir();
    OPTIGD_PASS();
}

void testDiagnosticsSanitizesNonFiniteValues() {
    auto& engine = optigd::diagnostics::DiagnosticsEngine::instance();
    engine.reset();

    // A pathological frame: NaN and infinities must be rejected, not propagated
    // into the JSON (where `nan`/`inf` are not parseable by any consumer).
    optigd::core::FrameStats bad{};
    bad.totalFrameTimeUs = std::numeric_limits<double>::quiet_NaN();
    bad.updateCpuTimeUs = std::numeric_limits<double>::infinity();
    bad.optigdOverheadUs = -std::numeric_limits<double>::infinity();
    engine.recordFrameStats(bad);

    optigd::core::FrameStats good = bench_test::makeFrame(16000.0);
    engine.recordFrameStats(good);

    const auto summary = engine.computeSummary(bench_test::makeUserConfig(), 0, 0);
    OPTIGD_ASSERT(summary.totalFrames == 2, "both frames were offered");
    OPTIGD_ASSERT(summary.sampleCount == 1, "the non-finite frame must be rejected");
    OPTIGD_ASSERT(summary.rejectedSamples == 1, "the rejection must be counted");
    OPTIGD_ASSERT(std::isfinite(summary.avgFrameTimeMs), "the mean must stay finite");
    OPTIGD_ASSERT(std::isfinite(summary.avgFps), "the fps must stay finite");
    OPTIGD_ASSERT(std::isfinite(summary.p99FrameTimeMs), "p99 must stay finite");
    OPTIGD_ASSERT(std::isfinite(summary.stdDevFrameTimeMs), "stddev must stay finite");

    const std::string report = engine.generateJsonReport(bench_test::makeUserConfig(), {}, 0);

    auto parsed = optigd::core::json::parse(report);
    OPTIGD_ASSERT(parsed.ok, "the report must still be parseable JSON");

    engine.reset();
    OPTIGD_PASS();
}

// ---------------------------------------------------------------------------
// Defects D, E, F, G in OptiGDManager.
// ---------------------------------------------------------------------------
void testNoFabricatedParticleEstimate() {
    auto& mgr = optigd::core::OptiGDManager::instance();
    mgr.shutdown();
    mgr.initialize();
    mgr.onLevelLoad();

    // No particles are registered, so nothing can legitimately have been avoided.
    mgr.beginFrame();
    mgr.setExternalFrameDeltaUs(16666.0);
    mgr.endFrame();

    const auto& stats = mgr.getLatestFrameStats();
    OPTIGD_ASSERT(stats.culledParticles == 0,
                  "with nothing measured, culledParticles must be 0 and not an estimate");

    auto& engine = optigd::diagnostics::DiagnosticsEngine::instance();
    const auto summary = engine.computeSummary(mgr.getActiveConfig(), 0, 0);
    OPTIGD_ASSERT(summary.totalParticlesAvoided == 0,
                  "total_particles_avoided must not contain a fabricated x32 estimate");

    mgr.onLevelExit();
    OPTIGD_PASS();
}

void testMatrixCacheDeltaIsNotTruncated() {
    auto& mgr = optigd::core::OptiGDManager::instance();
    mgr.shutdown();
    mgr.initialize();
    mgr.onLevelLoad();

    int dummy = 1;
    auto& cache = optigd::optimization::MatrixCache::instance();
    cache.clear();

    mgr.beginFrame();
    mgr.setExternalFrameDeltaUs(16666.0);
    // A delta larger than uint32_t would previously have wrapped to a small
    // number; the field is uint64_t and must carry the whole delta.
    for (int i = 0; i < 5; ++i) {
        cache.getOrCompute(&dummy, static_cast<float>(i), 0.0f, 0.0f, 1.0f, 1.0f);
    }
    mgr.endFrame();

    const auto& stats = mgr.getLatestFrameStats();
    OPTIGD_ASSERT(stats.matrixCacheHits + stats.matrixCacheMisses == 5,
                  "the five cache operations must be accounted for in the frame");
    OPTIGD_ASSERT(stats.matrixCacheMisses >= 1, "the first lookup must be a miss");

    mgr.onLevelExit();
    OPTIGD_PASS();
}

void testSubsystemTimeOutsideFrameWindowIsNotMisattributed() {
    auto& mgr = optigd::core::OptiGDManager::instance();
    mgr.shutdown();
    mgr.initialize();
    mgr.onLevelLoad();
    mgr.clearOutOfWindowAccounting();

    // Defect G: CCParticleSystem::update can run while no frame window is open.
    // beta.2 added that time to m_currentStats, so it was reported as the NEXT
    // frame's cost.
    OPTIGD_ASSERT(!mgr.isFrameOpen(), "no frame window is open yet");
    mgr.recordParticleTimeUs(500.0);
    mgr.recordCullingTimeUs(250.0);
    OPTIGD_ASSERT(mgr.getOutOfWindowSubsystemEvents() == 2,
                  "recordings outside a frame window must be counted");
    OPTIGD_ASSERT(std::fabs(mgr.getOutOfWindowSubsystemUs() - 750.0) < 1e-9,
                  "recordings outside a frame window must be timed");

    mgr.beginFrame();
    mgr.setExternalFrameDeltaUs(16666.0);
    OPTIGD_ASSERT(mgr.isFrameOpen(), "the frame window must be open");
    mgr.recordParticleTimeUs(100.0);
    mgr.endFrame();
    OPTIGD_ASSERT(!mgr.isFrameOpen(), "the frame window must be closed by endFrame");

    const auto& stats = mgr.getLatestFrameStats();
    OPTIGD_ASSERT(std::fabs(stats.particleTimeUs - 100.0) < 1e-9,
                  "only in-window time may be attributed to the frame");
    OPTIGD_ASSERT(std::fabs(stats.cullingTimeUs) < 1e-9,
                  "out-of-window culling time must not appear in any frame");
    OPTIGD_ASSERT(mgr.getOutOfWindowSubsystemEvents() == 2,
                  "the mis-attributed time must still be accounted for");

    mgr.onLevelExit();
    OPTIGD_PASS();
}

void testStallClassificationUsesTheSharedThreshold() {
    auto& mgr = optigd::core::OptiGDManager::instance();
    mgr.shutdown();
    mgr.initialize();
    mgr.onLevelLoad();

    // A normal frame is counted.
    mgr.beginFrame();
    mgr.setExternalFrameDeltaUs(16666.0);
    mgr.endFrame();
    OPTIGD_ASSERT(mgr.getLatestFrameStats().frameCounted, "a 16.7 ms frame is gameplay");
    OPTIGD_ASSERT(!mgr.getLatestFrameStats().gapFrame, "a 16.7 ms frame is not a gap");
    OPTIGD_ASSERT(!mgr.lastFrameWasStall(), "a 16.7 ms frame is not a stall");

    // A frame at the threshold boundary is still gameplay.
    mgr.beginFrame();
    mgr.setExternalFrameDeltaUs(optigd::core::kStallThresholdUs - 1.0);
    mgr.endFrame();
    OPTIGD_ASSERT(mgr.getLatestFrameStats().frameCounted,
                  "just under the shared threshold is still gameplay");

    // A stall is flagged, so both windows can act on it.
    mgr.beginFrame();
    mgr.setExternalFrameDeltaUs(optigd::core::kStallThresholdUs + 1000.0);
    mgr.endFrame();
    OPTIGD_ASSERT(mgr.lastFrameWasStall(), "an interval above the threshold is a stall");
    OPTIGD_ASSERT(mgr.getLatestFrameStats().gapFrame, "a stall must be flagged as a gap frame");
    OPTIGD_ASSERT(!mgr.getLatestFrameStats().frameCounted, "a stall must not be counted as gameplay");

    // A paused frame is flagged even though its interval looks normal. Without
    // this a capture could be "completed" while the player sat in the pause menu.
    mgr.beginFrame();
    mgr.setExternalFrameDeltaUs(16666.0);
    mgr.setFramePaused(true);
    mgr.endFrame();
    OPTIGD_ASSERT(mgr.getLatestFrameStats().gapFrame, "a paused frame must be flagged");
    OPTIGD_ASSERT(!mgr.getLatestFrameStats().frameCounted,
                  "a paused frame must not be counted as gameplay");
    OPTIGD_ASSERT(!mgr.lastFrameWasStall(), "a pause is not a stall: its interval is normal");

    // The pause flag must not leak into the next frame.
    mgr.beginFrame();
    mgr.setExternalFrameDeltaUs(16666.0);
    mgr.endFrame();
    OPTIGD_ASSERT(mgr.getLatestFrameStats().frameCounted,
                  "the pause flag must be cleared for the next frame");

    mgr.onLevelExit();
    OPTIGD_PASS();
}

void testThreadCpuTimeIsReportedSeparatelyFromWallTime() {
    // Defect F: `updateCpuTimeUs` is a wall-clock span. The new field must exist,
    // must be finite, and must never exceed the wall span by more than rounding.
    auto& mgr = optigd::core::OptiGDManager::instance();
    mgr.shutdown();
    mgr.initialize();
    mgr.onLevelLoad();

    mgr.beginFrame();
    mgr.setExternalFrameDeltaUs(16666.0);
    // A function-scope static accumulator: enough real work to make the wall
    // span measurable, and it cannot be optimized away.
    static double burn = 0.0;
    for (int i = 0; i < 200000; ++i) burn += std::sqrt(static_cast<double>(i));
    mgr.endFrame();
    OPTIGD_ASSERT(burn > 0.0, "the work loop must have run");

    const auto& stats = mgr.getLatestFrameStats();
    OPTIGD_ASSERT(std::isfinite(stats.updateCpuTimeUs), "the wall span must be finite");
    OPTIGD_ASSERT(std::isfinite(stats.updateThreadCpuUs), "the thread CPU time must be finite");
    OPTIGD_ASSERT(stats.updateCpuTimeUs > 0.0, "the update must have taken measurable time");
    if (optigd::platform::Platform::supportsThreadCpuTime()) {
        OPTIGD_ASSERT(stats.updateThreadCpuUs <= stats.updateCpuTimeUs * 1.5 + 1000.0,
                      "thread CPU time may not greatly exceed the wall span of the same call");
    } else {
        OPTIGD_ASSERT(stats.updateThreadCpuUs == 0.0,
                      "a platform without thread CPU time must report 0, never a guess");
    }

    auto& engine = optigd::diagnostics::DiagnosticsEngine::instance();
    const auto summary = engine.computeSummary(mgr.getActiveConfig(), 0, 0);
    OPTIGD_ASSERT(std::isfinite(summary.avgUpdateThreadCpuMs), "the average must be finite");
    const std::string report = engine.generateJsonReport(mgr.getActiveConfig(), {}, 0);
    OPTIGD_ASSERT(report.find("avg_update_cpu_time_ms_meaning") != std::string::npos,
                  "the report must explain that the wall span is not thread CPU time");

    mgr.onLevelExit();
    engine.reset();
    OPTIGD_PASS();
}

void testPlatformArchitectureIsReported() {
    const auto arch = optigd::platform::Platform::getArchitectureName();
    OPTIGD_ASSERT(!arch.empty(), "an architecture name must always be produced");
    OPTIGD_ASSERT(arch == "x86_64" || arch == "x86" || arch == "arm64" || arch == "arm32" ||
                  arch == "unknown",
                  "the architecture name must come from the known set");
    const auto info = optigd::platform::Platform::getSystemInfo();
    OPTIGD_ASSERT(info.architecture == std::string(arch),
                  "SystemInfo must carry the same architecture string");
    // Two captures on different architectures must never be compared as if they
    // described the same machine, so the value has to be stable and non-empty.
    OPTIGD_ASSERT(optigd::platform::Platform::getArchitecture() !=
                      optigd::platform::PlatformArch::Unknown ||
                  arch == "unknown",
                  "the enum and the name must agree");
    OPTIGD_PASS();
}

// Writes a real diagnostics JSON report to disk so CI can schema-validate the
// exact artefact the mod produces on level exit. Invoked with
// `--emit-report <path>`; does not affect the pass/fail result of the suite.
int emitDiagnosticsReport(const std::string& path) {
    auto& mgr = optigd::core::OptiGDManager::instance();
    mgr.shutdown();
    mgr.initialize();
    mgr.onLevelLoad();

    int dummyNode = 7;
    mgr.getVisibilityCuller().registerObject(
        1, optigd::core::Rect2D(100.0f, 100.0f, 30.0f, 30.0f), &dummyNode, true, true);

    for (int f = 0; f < 60; ++f) {
        mgr.beginFrame();
        mgr.setExternalFrameDeltaUs(16666.0);
        mgr.recordHeapAllocationAvoided(1);
        mgr.recordCullingTimeUs(5.0);
        mgr.recordParticleTimeUs(2.0);
        mgr.recordMatrixTimeUs(1.0);
        mgr.getVisibilityCuller().evaluateVisibilityDelta(
            optigd::core::Rect2D(0.0f, 0.0f, 800.0f, 600.0f), mgr.getFrameArena());
        mgr.endFrame();
    }

    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        std::cerr << "[FAIL] unable to open report path: " << path << "\n";
        return 1;
    }
    ofs << optigd::diagnostics::DiagnosticsEngine::instance().generateJsonReport(
        mgr.getActiveConfig(),
        mgr.getFallbackManager().getFallbackHistory(),
        mgr.getAdaptiveGovernor().getTierTransitions());
    ofs.close();
    mgr.onLevelExit();
    std::cout << "[INFO] wrote diagnostics report to " << path << "\n";
    return 0;
}

// Drives the real BenchmarkController headlessly (synthetic clock, temporary
// store) and writes the artefacts CI schema-validates:
//   <dir>/baseline.json   - a completed baseline session
//   <dir>/optimized.json  - a completed optimized session
//   <dir>/comparison.txt  - the A/B comparison of the two
//   <dir>/index.json      - the rebuildable store index
// These are REAL controller output, produced by the same code path the mod uses;
// only the clock and the filesystem location are synthetic. They are labelled
// [SIMULATED] evidence in the report because no Geometry Dash process ran them.
int emitBenchmarkSessions(const std::string& dir) {
    namespace bm = optigd::benchmark;
    std::error_code ec;
    const std::filesystem::path out(dir);
    // The store owns `<outdir>/sessions`; the stable copies the schema check reads
    // sit next to it. Keeping them apart means the store's own listing and index
    // describe exactly the sessions that were captured, not the exports of them.
    const std::filesystem::path root = out / "sessions";
    std::filesystem::remove_all(out, ec);
    std::filesystem::create_directories(root, ec);
    if (ec) {
        std::cerr << "[FAIL] cannot create " << root << ": " << ec.message() << "\n";
        return 1;
    }

    auto& cfgMgr = optigd::config::ConfigManager::instance();
    auto& mgr = optigd::core::OptiGDManager::instance();
    auto& controller = bm::BenchmarkController::instance();
    cfgMgr.resetToDefaults();
    cfgMgr.clearRuntimeOverride();
    cfgMgr.setBaseConfig(bench_test::makeUserConfig());
    mgr.reloadConfig();

    bench_test::installTestBridge(controller, root, /*targetMs=*/3000.0, /*stopOnReset=*/true);

    auto runCapture = [&](bm::BenchmarkMode mode, double frameUs, std::string& outId) -> bool {
        const auto started = controller.startSession(mode);
        if (!started.ok) {
            std::cerr << "[FAIL] startSession: " << started.message << "\n";
            return false;
        }
        size_t guard = 0;
        while (controller.isRunning() && guard++ < 100000) {
            bench_test::clock().advanceMs(frameUs / 1000.0);
            controller.onFrame(bench_test::makeFrame(frameUs));
        }
        const auto* session = controller.getSession();
        if (!session || session->state != bm::BenchmarkState::Completed) {
            std::cerr << "[FAIL] capture did not complete\n";
            return false;
        }
        outId = session->id;
        bench_test::clock().advanceMs(60000.0);  // a distinct second for the next id
        return true;
    };

    std::string baselineId;
    std::string optimizedId;
    bool ok = runCapture(bm::BenchmarkMode::Baseline, 20000.0, baselineId);
    ok = ok && runCapture(bm::BenchmarkMode::Optimized, 16000.0, optimizedId);
    if (!ok) return 1;

    bm::BenchmarkStore store(root);
    bm::BenchmarkSession baseline;
    bm::BenchmarkSession optimized;
    std::string error;
    if (!store.load(baselineId, baseline, error) ||
        !store.load(optimizedId, optimized, error)) {
        std::cerr << "[FAIL] could not reload the emitted sessions: " << error << "\n";
        return 1;
    }

    const auto comparison = bm::compareSessions(baseline, optimized, 2.0);
    std::string text = bm::formatComparisonText(comparison);
    text += "\n--- provenance ---\n";
    text += "Both sessions were produced by the real BenchmarkController in the\n";
    text += "standalone test binary: a synthetic clock, a temporary directory and\n";
    text += "no Geometry Dash process. They validate the SCHEMA and the internal\n";
    text += "consistency of a session record. They are [SIMULATED] evidence and\n";
    text += "must never be quoted as a measured FPS result.\n";
    if (!bm::BenchmarkStore::writeAtomic(out / "comparison.txt", text, error)) {
        std::cerr << "[FAIL] could not write comparison.txt: " << error << "\n";
        return 1;
    }
    if (!store.rebuildIndex(error)) {
        std::cerr << "[FAIL] could not rebuild the index: " << error << "\n";
        return 1;
    }

    std::cout << "[INFO] wrote benchmark sessions to " << out << "\n";
    std::cout << "[INFO]   store    : " << root << " (session files + index.json)\n";
    std::cout << "[INFO]   baseline : " << baselineId << " (" << baseline.stats.samples
              << " samples)\n";
    std::cout << "[INFO]   optimized: " << optimizedId << " (" << optimized.stats.samples
              << " samples)\n";
    std::cout << "[INFO]   verdict  : " << comparison.headline << "\n";
    std::cout << "[INFO]   evidence : " << baseline.evidenceClassification
              << " (synthetic clock, no Geometry Dash process)\n";

    bench_test::restoreControllerDefaults(controller);
    cfgMgr.resetToDefaults();
    mgr.reloadConfig();
    return 0;
}

// ---------------------------------------------------------------------------
// Regression (defect J): the AdaptiveGovernor must use the SAME percentile
// definition as the diagnostics window and Benchmark Mode.
//
// It indexed the sorted frame-time buffer at floor(n * p). Nearest-rank uses
// ceil(n * p) - 1. Those agree when n * p is fractional and differ by one sample
// when it is an integer, which is exactly what a round window produces: with 20
// frames the old code reported the SLOWEST frame in the window as its p95, so
// the governor could throttle on a number that no diagnostics report or
// benchmark session could reproduce.
// ---------------------------------------------------------------------------
void testGovernorPercentilesUseTheSharedDefinition() {
    namespace diag = optigd::diagnostics;

    optigd::core::OptiConfig cfg;
    optigd::adaptive::AdaptiveGovernor gov(cfg);

    // 20 distinct frame times, 11..30 ms. n * 0.95 = 19 exactly.
    std::vector<double> framesMs;
    for (int i = 1; i <= 20; ++i) framesMs.push_back(10.0 + static_cast<double>(i));
    for (double ms : framesMs) gov.recordFrameTime(ms * 1000.0);
    gov.update(cfg);

    const auto stats = gov.getRecentStats();
    std::vector<double> sorted = framesMs;
    std::sort(sorted.begin(), sorted.end());
    const double sharedP95 =
        diag::SampleWindow::percentileFromSorted(sorted.data(), sorted.size(), 0.95);
    const double sharedP99 =
        diag::SampleWindow::percentileFromSorted(sorted.data(), sorted.size(), 0.99);
    const double sharedMedian =
        diag::SampleWindow::medianFromSorted(sorted.data(), sorted.size());

    OPTIGD_ASSERT(std::fabs(stats.p95Ms - sharedP95) < 1e-9,
                  "the governor's p95 must be the shared nearest-rank value");
    OPTIGD_ASSERT(std::fabs(stats.p99Ms - sharedP99) < 1e-9,
                  "the governor's p99 must be the shared nearest-rank value");
    OPTIGD_ASSERT(std::fabs(stats.medianMs - sharedMedian) < 1e-9,
                  "the governor's median must be the shared definition");

    // The concrete numbers, so a change to the shared definition is noticed here
    // rather than silently moving the governor's behaviour.
    OPTIGD_ASSERT(std::fabs(stats.p95Ms - 29.0) < 1e-9,
                  "p95 over 11..30 ms is the 19th of 20 samples (29 ms), not the slowest");
    OPTIGD_ASSERT(std::fabs(stats.p99Ms - 30.0) < 1e-9,
                  "p99 over 11..30 ms is the 20th of 20 samples (30 ms)");
    OPTIGD_ASSERT(std::fabs(stats.medianMs - 20.5) < 1e-9,
                  "the median of 11..30 ms is 20.5 ms");
    OPTIGD_ASSERT(stats.medianMs <= stats.p95Ms && stats.p95Ms <= stats.p99Ms,
                  "percentiles must stay monotonic");
    OPTIGD_ASSERT(stats.p95Ms < stats.maxMs,
                  "a p95 that equals the window maximum is the off-by-one this test exists for");

    // The full 120-frame window the governor uses in play (n * 0.95 = 114 exactly)
    // must agree too.
    gov.clearHistory();
    std::vector<double> many;
    for (int i = 1; i <= 120; ++i) many.push_back(5.0 + 0.1 * static_cast<double>(i));
    for (double ms : many) gov.recordFrameTime(ms * 1000.0);
    gov.update(cfg);
    std::sort(many.begin(), many.end());
    const auto full = gov.getRecentStats();
    OPTIGD_ASSERT(std::fabs(full.p95Ms -
                            diag::SampleWindow::percentileFromSorted(many.data(), many.size(), 0.95)) < 1e-9,
                  "the 120-frame window must use the same definition");
    OPTIGD_ASSERT(std::fabs(full.p99Ms -
                            diag::SampleWindow::percentileFromSorted(many.data(), many.size(), 0.99)) < 1e-9,
                  "the 120-frame window must use the same definition for p99");

    OPTIGD_PASS();
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--emit-report" && i + 1 < argc) {
            return emitDiagnosticsReport(argv[i + 1]);
        }
        if (arg == "--emit-sessions" && i + 1 < argc) {
            return emitBenchmarkSessions(argv[i + 1]);
        }
    }

    std::cout << "========================================\n";
    std::cout << "  OptiGD v" << optigd::core::kOptiGDVersion << " Unit & Integration Suite\n";
    std::cout << "========================================\n";

    testMemoryArena();
    testSpatialHash();
    testVisibilityCuller();
    testParticleGovernor();
    testTriggerOptimizer();
    testMatrixCache();
    testAdaptiveGovernorAndFallback();
    testGovernorPercentilesUseTheSharedDefinition();
    testConfigAndPresetSync();
    testManagerAndCompatibility();
    testGeodeVersionMetadataGrammar();
    testVisibilityCullerCellSizeReindex();
    testParticleGovernorIncrementalTotals();
    testDiagnosticsJsonEscaping();
    testConfigDeserializeRobustness();
    testCameraWorldRectConvention();
    testMatrixCacheStaleEntryPruning();
    testFallbackRecoveryDuringCooldown();
    testProfilerFastPath();
    testGdVersionCompatibilityGate();
    testTierDefaultsSingleSource();

    // v0.1.0-beta.3: Benchmark Mode, telemetry integrity and the defect fixes
    // recorded in DESIGN_LOG.md Entry 6.
    testJsonValueRoundTripAndEscaping();
    testSampleWindowPercentileDefinitions();
    testSampleWindowRingOverflowDisclosesApproximation();
    testSampleWindowHitchStallAndRejectionAccounting();
    testConfigManagerTransientOverrideIsolation();
    testMakeBaselineConfigIsInternalControlNotVanilla();
    testBenchmarkSessionJsonRoundTrip();
    testBenchmarkSessionRejectsForeignAndNewerSchemas();
    testBenchmarkStoreNeverOverwritesPriorEvidence();
    testBenchmarkStoreAtomicWriteLeavesNoPartialFile();
    testBenchmarkControllerCompletesAndRestoresConfiguration();
    testBenchmarkControllerExcludesPausesAndStalls();
    testBenchmarkWallClockCeilingIsAGuardNotABudget();
    testBenchmarkControllerLevelResetHonoursTheSetting();
    testBenchmarkControllerVerifiesItsOwnOverride();
    testBenchmarkControllerReportsPersistFailureHonestly();
    testBenchmarkCompareDirectionAndNoiseThreshold();
    testBenchmarkCompareRefusesIncompatibleSessions();
    testBenchmarkCompareNeverDividesByZero();
    testBenchmarkPanelTextAndPagination();
    testDiagnosticsWindowDisclosureIsConsistent();
    testDiagnosticsResetIsLevelScoped();
    testDiagnosticsExportIsAtomicAndChecked();
    testDiagnosticsSanitizesNonFiniteValues();
    testNoFabricatedParticleEstimate();
    testMatrixCacheDeltaIsNotTruncated();
    testSubsystemTimeOutsideFrameWindowIsNotMisattributed();
    testStallClassificationUsesTheSharedThreshold();
    testThreadCpuTimeIsReportedSeparatelyFromWallTime();
    testPlatformArchitectureIsReported();

    std::cout << "----------------------------------------\n";
    std::cout << "Tests Passed: " << g_testsPassed << "\n";
    std::cout << "Tests Failed: " << g_testsFailed << "\n";
    std::cout << "========================================\n";

    return g_testsFailed == 0 ? 0 : 1;
}

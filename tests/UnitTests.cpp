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
#include "../src/benchmark/BenchmarkSuite.hpp"

#include <iostream>
#include <fstream>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

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

    OPTIGD_ASSERT(std::string(optigd::core::kOptiGDVersion) == "0.1.0-beta.2",
                  "OptiGD version constant must be 0.1.0-beta.2");
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
    OPTIGD_ASSERT(report.find("\"optigd_version\": \"0.1.0-beta.2\"") != std::string::npos,
                  "JSON report must contain optigd_version 0.1.0-beta.2");
    OPTIGD_ASSERT(report.find("\"geode_version\": \"v5.10.1\"") != std::string::npos,
                  "JSON report must contain geode_version v5.10.1");
    OPTIGD_ASSERT(report.find("\"median_frame_time_ms\"") != std::string::npos,
                  "JSON report must contain median_frame_time_ms");

    optigd::ui::DiagnosticsOverlay::instance().setEnabled(true);
    std::string hud = optigd::ui::DiagnosticsOverlay::instance().formatOverlayText(
        latest, mgr.getAdaptiveGovernor().getRecentStats(), mgr.getActiveConfig());
    OPTIGD_ASSERT(hud.find("[OptiGD v0.1.0-beta.2 Diagnostics]") != std::string::npos,
                  "Overlay header must display v0.1.0-beta.2");

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

    // The form actually used by this build must parse.
    auto current = parseVersionString(optigd::core::kOptiGDVersionTag);
    OPTIGD_ASSERT(current.valid,
                  "kOptiGDVersionTag must satisfy the Geode VersionInfo grammar");
    OPTIGD_ASSERT(current.major == 0 && current.minor == 1 && current.patch == 0,
                  "Parsed version must be 0.1.0");
    OPTIGD_ASSERT(current.tag.has_value() &&
                      *current.tag == optigd::core::geodever::VersionTagKind::Beta,
                  "Parsed tag must be Beta");
    OPTIGD_ASSERT(current.tagNumber.has_value() && *current.tagNumber == 2,
                  "Parsed tag number must be 2");
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

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--emit-report" && i + 1 < argc) {
            return emitDiagnosticsReport(argv[i + 1]);
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

    std::cout << "----------------------------------------\n";
    std::cout << "Tests Passed: " << g_testsPassed << "\n";
    std::cout << "Tests Failed: " << g_testsFailed << "\n";
    std::cout << "========================================\n";

    return g_testsFailed == 0 ? 0 : 1;
}

// Native unit and regression tests for the OptiGD core. Standalone (no Geode, no game).
//
// What these tests show: the module logic behaves as specified, including the defects
// fixed in v0.1.0-beta1. What they do NOT show: that any module changes gameplay or FPS in
// Geometry Dash. Runtime behaviour is covered by docs/GAMEPLAY_BENCHMARK_PROTOCOL.md.
//
// Each test returns false on the first failed assertion. main() runs every test and
// reports the total so one failure does not hide others.

#include "../src/adaptive/AdaptiveGovernor.hpp"
#include "../src/adaptive/FallbackManager.hpp"
#include "../src/compatibility/CompatibilityLayer.hpp"
#include "../src/config/ConfigManager.hpp"
#include "../src/config/Presets.hpp"
#include "../src/config/SettingsMapping.hpp"
#include "../src/core/MemoryArena.hpp"
#include "../src/core/SpatialHash.hpp"
#include "../src/core/Types.hpp"
#include "../src/core/Version.hpp"
#include "../src/diagnostics/DiagnosticsEngine.hpp"
#include "../src/diagnostics/FrameTimeRecorder.hpp"
#include "../src/optimization/MatrixCache.hpp"
#include "../src/optimization/ParticleGovernor.hpp"
#include "../src/optimization/TriggerOptimizer.hpp"
#include "../src/optimization/VisibilityCuller.hpp"
#include "../src/profiling/Profiler.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#define TEST_ASSERT(cond, msg)                                                     \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::cerr << "    FAILED: " << msg << " (" << __FILE__ << ":"          \
                      << __LINE__ << ")\n";                                        \
            return false;                                                          \
        }                                                                          \
    } while (0)

namespace core = optigd::core;
namespace opt = optigd::optimization;
namespace adp = optigd::adaptive;
namespace cfg = optigd::config;

// ---------------------------------------------------------------------------
// MemoryArena
// ---------------------------------------------------------------------------

bool testMemoryArenaBasics() {
    // Original assertions, unchanged in intent.
    core::MemoryArena arena(1024);

    void* p1 = arena.allocate(64, 8);
    TEST_ASSERT(p1 != nullptr, "p1 allocation failed");
    TEST_ASSERT(reinterpret_cast<uintptr_t>(p1) % 8 == 0, "p1 alignment incorrect");

    void* p2 = arena.allocate(128, 16);
    TEST_ASSERT(p2 != nullptr, "p2 allocation failed");
    TEST_ASSERT(reinterpret_cast<uintptr_t>(p2) % 16 == 0, "p2 alignment incorrect");
    TEST_ASSERT(arena.getUsedBytes() >= (64 + 128), "Used bytes not tracked properly");

    arena.reset();
    TEST_ASSERT(arena.getUsedBytes() == 0, "Used bytes not reset to 0");

    void* largePtr = arena.allocate(2048);
    TEST_ASSERT(largePtr != nullptr, "Multi-block allocation failed");
    TEST_ASSERT(arena.getCapacity() >= 2048, "Capacity did not expand");
    return true;
}

bool testMemoryArenaRejectsNonPowerOfTwoAlignment() {
    // Regression: alignment 3 used to return a pointer.
    core::MemoryArena arena(1024);
    TEST_ASSERT(arena.allocate(16, 3) == nullptr, "alignment 3 must be rejected");
    TEST_ASSERT(arena.allocate(16, 0) == nullptr, "alignment 0 must be rejected");
    TEST_ASSERT(arena.getUsedBytes() == 0, "rejected allocations must not consume memory");
    return true;
}

bool testMemoryArenaRejectsOverflowingSize() {
    // Regression: SIZE_MAX-4 used to take an unsafe path and miscount usage.
    core::MemoryArena arena(1024);
    (void)arena.allocate(32);
    const size_t usedBefore = arena.getUsedBytes();
    TEST_ASSERT(arena.allocate(std::numeric_limits<size_t>::max() - 4) == nullptr,
                "overflowing request must return nullptr");
    TEST_ASSERT(arena.getUsedBytes() == usedBefore, "failed request must not change usage");
    TEST_ASSERT(arena.allocate(core::MemoryArena::kMaxBlockBytes + 1) == nullptr,
                "request above the block guard must return nullptr");
    return true;
}

// ---------------------------------------------------------------------------
// SpatialHash
// ---------------------------------------------------------------------------

bool testSpatialHashQuery() {
    // Original assertions: 2 hits, no duplicates for an item spanning cells.
    core::SpatialHash hash(100.0f, 64);
    int tag1 = 101, tag2 = 102, tag3 = 103;
    hash.insert(core::Rect2D(50.0f, 50.0f, 20.0f, 20.0f), &tag1);
    hash.insert(core::Rect2D(90.0f, 90.0f, 30.0f, 30.0f), &tag2);
    hash.insert(core::Rect2D(5000.0f, 5000.0f, 10.0f, 10.0f), &tag3);

    std::vector<void*> results;
    hash.query(core::Rect2D(0.0f, 0.0f, 200.0f, 200.0f), results);
    TEST_ASSERT(results.size() == 2, "Expected 2 visible items");
    size_t countTag2 = 0;
    for (void* ptr : results) {
        if (*static_cast<int*>(ptr) == 102) countTag2++;
    }
    TEST_ASSERT(countTag2 == 1, "Item spanning multiple cells was duplicated");
    return true;
}

bool testSpatialHashMovedItemIsFoundAtNewLocation() {
    // Regression: update() left stale index entries, so the item was lost at its new cell.
    core::SpatialHash hash(100.0f);
    int tag = 7;
    const uint32_t id = hash.insert(core::Rect2D(10.0f, 10.0f, 10.0f, 10.0f), &tag);
    TEST_ASSERT(hash.update(id, core::Rect2D(900.0f, 900.0f, 10.0f, 10.0f)), "update failed");

    std::vector<void*> atNew, atOld;
    hash.query(core::Rect2D(890.0f, 890.0f, 30.0f, 30.0f), atNew);
    hash.query(core::Rect2D(0.0f, 0.0f, 50.0f, 50.0f), atOld);
    TEST_ASSERT(atNew.size() == 1, "moved item not found at its new location");
    TEST_ASSERT(atOld.empty(), "moved item still found at its old location");
    return true;
}

bool testSpatialHashRemoveAndNegativeCoordinates() {
    core::SpatialHash hash(64.0f);
    int a = 1, b = 2;
    const uint32_t idA = hash.insert(core::Rect2D(-300.0f, -300.0f, 20.0f, 20.0f), &a);
    hash.insert(core::Rect2D(-310.0f, -310.0f, 5.0f, 5.0f), &b);

    std::vector<void*> r;
    hash.query(core::Rect2D(-320.0f, -320.0f, 40.0f, 40.0f), r);
    TEST_ASSERT(r.size() == 2, "negative-coordinate items not both found");

    TEST_ASSERT(hash.remove(idA), "remove failed");
    TEST_ASSERT(!hash.remove(idA), "second remove must fail");
    r.clear();
    hash.query(core::Rect2D(-320.0f, -320.0f, 40.0f, 40.0f), r);
    TEST_ASSERT(r.size() == 1 && r[0] == &b, "removed item still returned");
    TEST_ASSERT(hash.getItemCount() == 1, "live count not decremented on remove");
    return true;
}

bool testSpatialHashRejectsNonFiniteInput() {
    core::SpatialHash hash(64.0f);
    int tag = 0;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    TEST_ASSERT(hash.insert(core::Rect2D(nan, 0.0f, 1.0f, 1.0f), &tag) == core::SpatialHash::kInvalidId,
                "NaN bounds must be rejected");
    TEST_ASSERT(hash.insert(core::Rect2D(0.0f, 0.0f, inf, 1.0f), &tag) == core::SpatialHash::kInvalidId,
                "infinite bounds must be rejected");
    TEST_ASSERT(hash.getItemCount() == 0, "rejected items must not be counted");
    return true;
}

// ---------------------------------------------------------------------------
// VisibilityCuller
// ---------------------------------------------------------------------------

bool testVisibilityCullerCounts() {
    // Original assertions: exactly one visible object (A), one culled.
    opt::VisibilityCuller culler(128.0f);
    culler.setPadding(50.0f, 50.0f);
    int objA = 1, objB = 2;
    culler.registerObject(core::Rect2D(100.0f, 100.0f, 30.0f, 30.0f), &objA);
    culler.registerObject(core::Rect2D(2000.0f, 100.0f, 30.0f, 30.0f), &objB);

    std::vector<void*> visible;
    culler.cullAgainstCamera(core::Rect2D(0.0f, 0.0f, 500.0f, 500.0f), visible);
    TEST_ASSERT(visible.size() == 1, "Expected exactly 1 visible object");
    TEST_ASSERT(*static_cast<int*>(visible[0]) == 1, "Expected objA to be visible");
    TEST_ASSERT(culler.getLastCulledCount() == 1, "Expected 1 culled object");
    TEST_ASSERT(culler.getLastVisibleCount() == 1, "Expected 1 visible count");
    return true;
}

bool testVisibilityCullerUpdateAndUnregister() {
    opt::VisibilityCuller culler(128.0f);
    culler.setPadding(0.0f, 0.0f);
    int obj = 5;
    const auto h = culler.registerObject(core::Rect2D(0.0f, 0.0f, 10.0f, 10.0f), &obj);
    TEST_ASSERT(h != opt::VisibilityCuller::kInvalidHandle, "registration failed");

    TEST_ASSERT(culler.updateObjectBounds(h, core::Rect2D(1000.0f, 0.0f, 10.0f, 10.0f)), "update failed");
    std::vector<void*> visible;
    culler.cullAgainstCamera(core::Rect2D(0.0f, 0.0f, 100.0f, 100.0f), visible);
    TEST_ASSERT(visible.empty(), "object moved out of view must be culled");
    TEST_ASSERT(culler.getLastCulledCount() == 1, "moved object must count as culled");

    TEST_ASSERT(culler.unregisterObject(h), "unregister failed");
    culler.cullAgainstCamera(core::Rect2D(0.0f, 0.0f, 100.0f, 100.0f), visible);
    TEST_ASSERT(culler.getTotalObjectCount() == 0, "unregistered object still counted");
    TEST_ASSERT(culler.getLastCulledCount() == 0, "no objects should remain to cull");
    return true;
}

// ---------------------------------------------------------------------------
// ParticleGovernor
// ---------------------------------------------------------------------------

bool testParticleGovernorOriginalBehaviour() {
    // Original assertions: critical never culled; offscreen cosmetic culled; onscreen
    // cosmetic runs at density 0.5.
    opt::ParticleGovernor gov(0.5f, 1000);
    const auto crit = gov.registerEmitter(core::Point2D(5000.0f, 5000.0f), 50.0f, opt::ParticlePriority::Critical);
    const auto offCosm = gov.registerEmitter(core::Point2D(5000.0f, 5000.0f), 50.0f, opt::ParticlePriority::Cosmetic);
    const auto onCosm = gov.registerEmitter(core::Point2D(100.0f, 100.0f), 50.0f, opt::ParticlePriority::Cosmetic);
    const core::Rect2D cam(0.0f, 0.0f, 800.0f, 600.0f);

    TEST_ASSERT(gov.evaluateEmitter(crit, cam).active, "Critical emitter was culled offscreen");

    TEST_ASSERT(!gov.evaluateEmitter(offCosm, cam).active, "Cosmetic offscreen emitter was not culled");

    const auto onDecision = gov.evaluateEmitter(onCosm, cam);
    TEST_ASSERT(onDecision.active, "Cosmetic onscreen emitter was incorrectly culled");
    TEST_ASSERT(std::abs(onDecision.emissionMultiplier - 0.5f) < 0.01f, "Density scale not applied");
    return true;
}

bool testParticleGovernorCriticalNeverThrottled() {
    opt::ParticleGovernor gov(0.1f, 128);
    const auto crit = gov.registerEmitter(core::Point2D(0.0f, 0.0f), 10.0f, opt::ParticlePriority::Critical);
    gov.recordActiveParticles(crit, 100000);
    const auto d = gov.evaluateEmitter(crit, core::Rect2D(0.0f, 0.0f, 10.0f, 10.0f));
    TEST_ASSERT(d.active && d.emissionMultiplier == 1.0f, "critical emitter must run at full density");
    return true;
}

bool testParticleGovernorHysteresisBand() {
    // Sleep margin 240, wake margin 120. Emitter box spans x = 1000..1020.
    // The state changes only when the camera moves across a margin boundary; inside
    // the band the state is kept, so emitters near the edge do not flap.
    opt::ParticleGovernor gov(0.5f, 4096);
    gov.setHysteresis(240.0f, 120.0f);
    const auto e = gov.registerEmitter(core::Point2D(1010.0f, 100.0f), 10.0f, opt::ParticlePriority::Cosmetic);

    // Camera right edge at 800: gap to the emitter is 200 px, inside the sleep margin.
    TEST_ASSERT(gov.evaluateEmitter(e, core::Rect2D(0.0f, 0.0f, 800.0f, 600.0f)).active,
                "awake emitter inside the sleep margin must stay awake");
    TEST_ASSERT(gov.getSleepTransitions() == 0, "no transition expected inside the band");

    // Camera right edge at 100: gap 900 px, beyond the sleep margin: it sleeps.
    const auto slept = gov.evaluateEmitter(e, core::Rect2D(0.0f, 0.0f, 100.0f, 600.0f));
    TEST_ASSERT(!slept.active && slept.justSlept, "emitter beyond the sleep margin must sleep");

    // Camera back to 800: gap 200 px, inside the band but beyond the wake margin: stays asleep.
    const auto band = gov.evaluateEmitter(e, core::Rect2D(0.0f, 0.0f, 800.0f, 600.0f));
    TEST_ASSERT(!band.active && !band.justWoke, "sleeping emitter in the band must stay asleep");

    // Camera right edge at 950: gap 50 px, inside the wake margin: wakes.
    const auto woke = gov.evaluateEmitter(e, core::Rect2D(0.0f, 0.0f, 950.0f, 600.0f));
    TEST_ASSERT(woke.active && woke.justWoke, "emitter inside the wake margin must wake");
    return true;
}

bool testParticleGovernorWakeHasNoCatchUp() {
    opt::ParticleGovernor gov(0.5f, 4096);
    gov.setHysteresis(100.0f, 50.0f);
    const auto e = gov.registerEmitter(core::Point2D(5000.0f, 0.0f), 10.0f, opt::ParticlePriority::Cosmetic);
    const core::Rect2D cam(0.0f, 0.0f, 800.0f, 600.0f);
    const auto slept = gov.evaluateEmitter(e, cam);
    TEST_ASSERT(slept.justSlept && !slept.active, "emitter should fall asleep off-screen");

    // Move the camera onto the emitter: it wakes, with the normal density and no catch-up.
    const core::Rect2D onScreen(4990.0f, 0.0f, 100.0f, 100.0f);
    const auto woke = gov.evaluateEmitter(e, onScreen);
    TEST_ASSERT(woke.active && woke.justWoke, "emitter must wake when the camera reaches it");
    TEST_ASSERT(std::abs(woke.emissionMultiplier - 0.5f) < 1e-6f, "wake must not grant a burst above base density");
    return true;
}

bool testParticleGovernorIncrementalTotals() {
    opt::ParticleGovernor gov(0.5f, 4096);
    gov.setHysteresis(10.0f, 5.0f);
    const auto a = gov.registerEmitter(core::Point2D(0.0f, 0.0f), 5.0f, opt::ParticlePriority::Cosmetic);
    const auto b = gov.registerEmitter(core::Point2D(9000.0f, 0.0f), 5.0f, opt::ParticlePriority::Cosmetic);
    gov.recordActiveParticles(a, 300);
    gov.recordActiveParticles(b, 200);
    TEST_ASSERT(gov.getTotalActiveParticles() == 500, "awake total must include both emitters");

    // Camera covers only `a`: b sleeps and its 200 particles leave the awake total.
    const core::Rect2D cam(-10.0f, -10.0f, 40.0f, 40.0f);
    gov.evaluateEmitter(a, cam);
    gov.evaluateEmitter(b, cam);
    TEST_ASSERT(gov.getSleepingEmitterCount() == 1, "exactly one emitter should sleep");
    TEST_ASSERT(gov.getTotalActiveParticles() == 300, "sleeping emitter must leave the awake total");

    gov.recordActiveParticles(a, 100);
    TEST_ASSERT(gov.getTotalActiveParticles() == 100, "update of an awake emitter must adjust the total");

    TEST_ASSERT(gov.unregisterEmitter(a), "unregister failed");
    TEST_ASSERT(gov.getTotalActiveParticles() == 0, "unregister must remove the emitter's particles");
    return true;
}

bool testParticleGovernorBudgetReducesMultiplier() {
    opt::ParticleGovernor gov(1.0f, 128); // minimum budget
    const auto big = gov.registerEmitter(core::Point2D(0.0f, 0.0f), 5.0f, opt::ParticlePriority::Standard);
    gov.recordActiveParticles(big, 500);
    const auto d = gov.evaluateEmitter(big, core::Rect2D(0.0f, 0.0f, 10.0f, 10.0f));
    TEST_ASSERT(d.active && std::abs(d.emissionMultiplier - 0.5f) < 1e-6f,
                "over-budget standard emitter must be halved");
    return true;
}

// ---------------------------------------------------------------------------
// TriggerOptimizer
// ---------------------------------------------------------------------------

bool testTriggerOptimizerOriginalBehaviour() {
    opt::TriggerOptimizer trig(500.0f);
    int t1 = 1, t2 = 2, t3 = 3;
    trig.registerTrigger(0, 0, opt::TriggerType::TouchOrPosition, 100.0f, &t1);
    trig.registerTrigger(1, 0, opt::TriggerType::TouchOrPosition, 300.0f, &t2);
    trig.registerTrigger(2, 0, opt::TriggerType::TouchOrPosition, 2000.0f, &t3);
    TEST_ASSERT(trig.prepareLevel(), "prepareLevel failed");

    std::vector<void*> executed;
    trig.evaluate(150.0f, executed);
    TEST_ASSERT(executed.size() == 1, "Expected 1 trigger executed at X=150");
    TEST_ASSERT(*static_cast<int*>(executed[0]) == 1, "Expected trig1 to fire");

    executed.clear();
    trig.evaluate(150.0f, executed);
    TEST_ASSERT(executed.empty(), "One-shot trigger fired twice");

    trig.evaluate(350.0f, executed);
    TEST_ASSERT(executed.size() == 1, "Expected trig2 to fire at X=350");
    TEST_ASSERT(*static_cast<int*>(executed[0]) == 2, "Expected trig2 to fire");
    return true;
}

bool testTriggerOptimizerRearmsOnRestart() {
    // Regression: triggers stayed completed after a restart and never fired again.
    opt::TriggerOptimizer trig(500.0f);
    int t1 = 1;
    trig.registerTrigger(0, 0, opt::TriggerType::TouchOrPosition, 100.0f, &t1);
    trig.prepareLevel();

    std::vector<void*> out;
    TEST_ASSERT(trig.evaluate(150.0f, out) == 1, "first run must fire");
    TEST_ASSERT(trig.evaluate(150.0f, out) == 0, "same run must not refire");

    TEST_ASSERT(trig.prepareLevel(), "restart prepareLevel failed");
    TEST_ASSERT(trig.evaluate(150.0f, out) == 1, "trigger must fire again after a level restart");
    return true;
}

bool testTriggerOptimizerRequiresPrepareAfterRegistration() {
    // Regression: registering after prepare left dangling pointers in the sorted index.
    opt::TriggerOptimizer trig(500.0f);
    int t1 = 1, t2 = 2;
    trig.registerTrigger(0, 0, opt::TriggerType::TouchOrPosition, 100.0f, &t1);
    trig.prepareLevel();
    trig.registerTrigger(1, 0, opt::TriggerType::TouchOrPosition, 200.0f, &t2);

    std::vector<void*> out;
    TEST_ASSERT(trig.evaluate(300.0f, out) == 0 && out.empty(), "unprepared index must not evaluate");
    TEST_ASSERT(!trig.isPrepared(), "registration must invalidate the prepared index");

    trig.prepareLevel();
    TEST_ASSERT(trig.evaluate(300.0f, out) == 2, "both triggers must fire after re-preparing");
    return true;
}

bool testTriggerOptimizerSpawnGroups() {
    opt::TriggerOptimizer trig;
    int a = 1, b = 2, c = 3;
    trig.registerTrigger(0, 7, opt::TriggerType::Spawn, 0.0f, &a);
    trig.registerTrigger(1, 7, opt::TriggerType::Spawn, 0.0f, &b);
    trig.registerTrigger(2, 9, opt::TriggerType::Spawn, 0.0f, &c);
    trig.prepareLevel();
    std::vector<void*> out;
    TEST_ASSERT(trig.invokeSpawnGroup(7, out) == 2, "group 7 must invoke two triggers");
    TEST_ASSERT(trig.invokeSpawnGroup(42, out) == 0, "unknown group must invoke nothing");
    return true;
}

// ---------------------------------------------------------------------------
// MatrixCache
// ---------------------------------------------------------------------------

bool testMatrixCacheMarkDirtyInvalidates() {
    // Regression: getCached() after markDirty() returned a stale hit.
    opt::MatrixCache cache;
    int node = 0;
    const opt::LocalTransformInputs in{10.0f, 20.0f, 0.0f, 1.0f, 1.0f};
    const auto m = opt::AffineMatrix{1.0f, 0.0f, 0.0f, 1.0f, 10.0f, 20.0f};
    cache.store(&node, in, m);

    opt::AffineMatrix out;
    TEST_ASSERT(cache.getCached(&node, in, out) && out == m, "stored entry must hit");
    cache.markDirty(&node);
    TEST_ASSERT(!cache.getCached(&node, in, out), "dirty entry must miss");
    return true;
}

bool testMatrixCacheInputChangeMisses() {
    opt::MatrixCache cache;
    int node = 0;
    cache.store(&node, {1.0f, 2.0f, 0.0f, 1.0f, 1.0f}, opt::AffineMatrix::identity());
    opt::AffineMatrix out;
    TEST_ASSERT(!cache.getCached(&node, {1.5f, 2.0f, 0.0f, 1.0f, 1.0f}, out), "changed input must miss");
    TEST_ASSERT(cache.getMisses() == 1, "miss counter must advance");
    cache.invalidate(&node);
    TEST_ASSERT(cache.getCachedCount() == 0, "invalidate must remove the entry");
    return true;
}

bool testAffineConcatComposition() {
    // Compose translate(10,0) then scale(2,2).
    const opt::AffineMatrix translate{1, 0, 0, 1, 10, 0};
    const opt::AffineMatrix scale{2, 0, 0, 2, 0, 0};
    const auto combined = translate.concat(scale);
    // Cocos convention: concat(t1, t2) applies t1 first. (1,1) -> (11,1) -> (22,2).
    TEST_ASSERT(std::abs(combined.a * 1 + combined.c * 1 + combined.tx - 22.0f) < 1e-5f, "x mapping wrong");
    TEST_ASSERT(std::abs(combined.b * 1 + combined.d * 1 + combined.ty - 2.0f) < 1e-5f, "y mapping wrong");
    TEST_ASSERT(opt::AffineMatrix::identity().concat(translate) == translate, "identity must be neutral");
    return true;
}

// ---------------------------------------------------------------------------
// AdaptiveGovernor and FallbackManager
// ---------------------------------------------------------------------------

bool testAdaptiveGovernorOriginalBehaviour() {
    // Original assertions: heavy load from Balanced downgrades to LowEnd; jitter triggers
    // a fallback which is marked active.
    core::OptiConfig user{};
    user.tier = core::PerformanceTier::Balanced;
    user.targetFrameRate = 60.0f;
    user.targetFrameTimeMs = 16.666f;

    adp::AdaptiveGovernor gov(user);
    adp::FallbackManager fallback(4.0f, 10);

    for (int i = 0; i < 40; ++i) {
        gov.recordFrameInterval(35000.0);
    }
    gov.update(user);
    TEST_ASSERT(gov.getCurrentTier() == core::PerformanceTier::LowEnd,
                "AdaptiveGovernor failed to downgrade to LowEnd under heavy load");

    adp::WindowStats jitter = gov.getRecentStats();
    jitter.stdDevMs = 6.0;
    jitter.fps = 40.0;
    jitter.sampleCount = 40;
    TEST_ASSERT(fallback.update(100, jitter, user), "FallbackManager failed to trigger on high jitter");
    TEST_ASSERT(fallback.isFallbackActive(), "Fallback not marked active");
    return true;
}

bool testAdaptiveGovernorNeverMutatesUserConfig() {
    // Regression: the governor used to write preset values into the user's config.
    core::OptiConfig user{};
    user.tier = core::PerformanceTier::Balanced;
    user.particleDensityScale = 0.9f; // a user value that is not a preset
    adp::AdaptiveGovernor gov(user);
    for (int i = 0; i < 40; ++i) gov.recordFrameInterval(40000.0);
    gov.update(user);

    TEST_ASSERT(user.tier == core::PerformanceTier::Balanced, "user tier must not change");
    TEST_ASSERT(user.particleDensityScale == 0.9f, "user tuning must not change");
    const auto effective = gov.effectiveConfig(user);
    TEST_ASSERT(effective.tier == core::PerformanceTier::LowEnd, "effective tier must reflect the governor");
    TEST_ASSERT(std::abs(effective.particleDensityScale - 0.5f) < 1e-6f, "effective tuning must follow the preset");
    return true;
}

bool testAdaptiveGovernorIgnoresInvalidSamplesAndCustom() {
    core::OptiConfig user{};
    user.tier = core::PerformanceTier::Custom;
    user.particleDensityScale = 0.3f;
    adp::AdaptiveGovernor gov(user);

    gov.recordFrameInterval(0.0);                       // discontinuity: not a sample
    gov.recordFrameInterval(std::numeric_limits<double>::quiet_NaN());
    gov.recordFrameInterval(-5.0);
    for (int i = 0; i < 60; ++i) gov.recordFrameInterval(60000.0);
    gov.update(user);

    TEST_ASSERT(gov.getRecentStats().sampleCount == 60, "only valid positive samples are counted");
    TEST_ASSERT(gov.getCurrentTier() == core::PerformanceTier::Custom, "Custom must never auto-scale");
    TEST_ASSERT(std::abs(gov.effectiveConfig(user).particleDensityScale - 0.3f) < 1e-6f,
                "Custom tuning must stay as the user set it");
    return true;
}

bool testFallbackEscalatesAndRecovers() {
    core::OptiConfig user{};
    user.targetFrameRate = 60.0f;
    adp::FallbackManager fb(4.0f, 2);

    adp::WindowStats jitter;
    jitter.stdDevMs = 8.0;
    jitter.fps = 30.0;
    jitter.sampleCount = 60;
    TEST_ASSERT(fb.update(1, jitter, user), "first jitter must escalate to level 1");
    TEST_ASSERT(fb.getLevel() == 1, "level must be 1");

    core::OptiConfig eff = user;
    fb.apply(eff);
    TEST_ASSERT(eff.particleDensityScale <= 0.25f, "level 1 must cap density");
    TEST_ASSERT(user.particleDensityScale == 0.5f, "fallback must never edit the user config");

    adp::WindowStats stable;
    stable.stdDevMs = 0.5;
    stable.fps = 60.0;
    stable.sampleCount = 60;
    // Cooldown of 2 frames blocks changes for two calls, then recovery happens.
    TEST_ASSERT(!fb.update(2, stable, user), "cooldown must block the first call");
    TEST_ASSERT(!fb.update(3, stable, user), "cooldown must block the second call");
    TEST_ASSERT(fb.update(4, stable, user), "stable frames must recover after cooldown");
    TEST_ASSERT(fb.getLevel() == 0, "recovery must return to level 0");
    return true;
}

bool testFallbackUserOverride() {
    core::OptiConfig user{};
    user.targetFrameRate = 60.0f;
    adp::FallbackManager fb(4.0f, 0);
    adp::WindowStats jitter;
    jitter.stdDevMs = 9.0;
    jitter.fps = 20.0;
    jitter.sampleCount = 60;
    TEST_ASSERT(fb.update(1, jitter, user), "precondition: fallback active");
    fb.setUserOverride(true);
    TEST_ASSERT(fb.getLevel() == 0, "override must clear active levels");
    TEST_ASSERT(!fb.update(2, jitter, user), "override must block escalation");
    return true;
}

// ---------------------------------------------------------------------------
// Configuration, presets, settings mapping
// ---------------------------------------------------------------------------

bool testConfigSerializationRoundTrip() {
    // Original assertions: serialize, change tier, deserialize restores LowEnd.
    auto& mgr = cfg::ConfigManager::instance();
    mgr.resetToDefaults();
    mgr.applyTierPreset(core::PerformanceTier::LowEnd);
    const std::string serialized = mgr.serialize();
    TEST_ASSERT(!serialized.empty(), "Serialization produced empty string");

    mgr.applyTierPreset(core::PerformanceTier::HighEnd);
    TEST_ASSERT(mgr.getConfig().tier == core::PerformanceTier::HighEnd, "HighEnd not set");
    TEST_ASSERT(mgr.deserialize(serialized), "Deserialization failed");
    TEST_ASSERT(mgr.getConfig().tier == core::PerformanceTier::LowEnd, "Tier not restored to LowEnd");
    return true;
}

bool testConfigDeserializeRejectsGarbageWithoutThrowing() {
    // Regression: stoi/stof threw on malformed values and escaped the settings path.
    auto& mgr = cfg::ConfigManager::instance();
    mgr.resetToDefaults();
    const auto before = mgr.getConfig();

    const std::string junk = "tier=abc\ntargetFps=fast\nmaxActiveParticles=99999999999999999999\n"
                             "cameraPaddingX=nan\nno-equals-here\n=\n";
    bool ok = true;
    try {
        ok = mgr.deserialize(junk);
    } catch (...) {
        TEST_ASSERT(false, "deserialize must not throw on malformed input");
    }
    TEST_ASSERT(!ok, "input with no valid key must report failure");
    TEST_ASSERT(mgr.getConfig().maxActiveParticles == before.maxActiveParticles, "invalid values must be ignored");
    TEST_ASSERT(mgr.getConfig().targetFrameRate == before.targetFrameRate, "invalid fps must be ignored");
    return true;
}

bool testPresetsAndTierParsing() {
    TEST_ASSERT(!cfg::presetTuning(core::PerformanceTier::Custom).has_value(), "Custom has no preset values");
    const auto low = cfg::presetTuning(core::PerformanceTier::LowEnd);
    TEST_ASSERT(low.has_value() && low->maxActiveParticles == 2048, "LowEnd preset changed unexpectedly");

    core::OptiConfig c{};
    cfg::applyPresetTier(c, core::PerformanceTier::Custom);
    TEST_ASSERT(c.tier == core::PerformanceTier::Custom && c.maxActiveParticles == core::OptiConfig{}.maxActiveParticles,
                "Custom must keep the user's tuning values");

    TEST_ASSERT(cfg::parseTier("Balanced") == core::PerformanceTier::Balanced, "parse Balanced");
    TEST_ASSERT(!cfg::parseTier("balanced").has_value(), "tier names are case-sensitive");
    TEST_ASSERT(!cfg::parseTier("").has_value(), "empty tier name is invalid");
    return true;
}

bool testSettingsMappingUnknownTierWarns() {
    cfg::SettingsSnapshot s;
    s.tierName = "Ultra";
    const auto r = cfg::resolveSettings(s, core::OptiConfig{});
    TEST_ASSERT(r.config.tier == core::PerformanceTier::LowEnd, "unknown tier must fall back to LowEnd");
    TEST_ASSERT(!r.warning.empty(), "unknown tier must produce a warning");
    return true;
}

bool testSettingsMappingFlagsAndCustomClamp() {
    cfg::SettingsSnapshot s;
    s.tierName = "Custom";
    s.enableCulling = true;
    s.enableParticleLimits = false;
    s.enableAdaptive = false;
    s.customParticleDensity = 7.0;      // clamped to 1.0
    s.customMaxParticles = -5;          // clamped to 128
    s.customPaddingX = 100000;          // clamped to 600
    const auto r = cfg::resolveSettings(s, core::OptiConfig{});

    TEST_ASSERT(r.warning.empty(), "valid tier must not warn");
    TEST_ASSERT(r.config.flags.enableSpatialCulling, "culling flag must map");
    TEST_ASSERT(!r.config.flags.enableParticleThrottling, "particle flag must map");
    TEST_ASSERT(!r.config.flags.enableAdaptiveGovernor, "adaptive flag must map");
    TEST_ASSERT(!r.config.flags.enableTriggerIndexing && !r.config.flags.enableMatrixCaching,
                "unexposed experimental flags must stay off");
    TEST_ASSERT(std::abs(r.config.particleDensityScale - 1.0f) < 1e-6f, "density must clamp to 1.0");
    TEST_ASSERT(r.config.maxActiveParticles == 128u, "particle budget must clamp to 128");
    TEST_ASSERT(std::abs(r.config.cameraPaddingX - 600.0f) < 1e-6f, "padding must clamp to 600");
    return true;
}

bool testDefaultFlagsMatchDecision() {
    // Decision recorded in DESIGN_LOG.md (Entry 3): measurement-first defaults.
    const core::OptimizationFlags f{};
    TEST_ASSERT(!f.enableSpatialCulling, "spatial culling default must be OFF");
    TEST_ASSERT(!f.enableParticleThrottling, "particle throttling default must be OFF");
    TEST_ASSERT(!f.enableTriggerIndexing, "trigger indexing default must be OFF");
    TEST_ASSERT(!f.enableMatrixCaching, "matrix caching default must be OFF");
    TEST_ASSERT(f.enableAdaptiveGovernor, "adaptive governor default must be ON");
    TEST_ASSERT(!f.enableDiagnosticsOverlay, "diagnostics overlay default must be OFF");
    TEST_ASSERT(!f.enableFrameTimeRecorder, "frame-time recorder default must be OFF");
    return true;
}

// ---------------------------------------------------------------------------
// Versions, compatibility, profiler, diagnostics, recorder
// ---------------------------------------------------------------------------

bool testVersionStringsFollowGeodeGrammar() {
    const std::string geode = optigd::kOptiGDGeodeVersion;
    TEST_ASSERT(geode == "v0.1.0-beta.1", "Geode version must be v0.1.0-beta.1");
    TEST_ASSERT(std::string(optigd::kOptiGDVersion) == "0.1.0-beta1", "release id must be 0.1.0-beta1");
    TEST_ASSERT(std::string(optigd::kGeometryDashVersion) == "2.2081", "GD version must be 2.2081");
    return true;
}

bool testCompatibilityConflictAndDefaults() {
    // Original assertions kept; compatibility now reads versions from Version.hpp.
    auto& comp = optigd::compat::CompatibilityLayer::instance();
    comp.initialize(optigd::kGeometryDashVersion, optigd::kOptiGDGeodeVersion);
    TEST_ASSERT(comp.getGDVersion() == "2.2081", "GD version must come from the caller, not a hard-coded value");
    TEST_ASSERT(comp.getGeodeVersion() == "v0.1.0-beta.1", "Geode version must be passed through");

    TEST_ASSERT(comp.isFeatureUsable("spatial_culling"), "spatial_culling should be usable initially");
    comp.reportConflict("spatial_culling", "Third-party camera mod hook detected");
    TEST_ASSERT(!comp.isFeatureUsable("spatial_culling"), "spatial_culling should be disabled on conflict");
    TEST_ASSERT(comp.isFeatureUsable("particle_governor"), "particle_governor should remain unaffected");
    comp.enableFeature("spatial_culling");
    return true;
}

bool testProfilerScopesDoNotCollide() {
    // Regression: OPTIGD_PROFILE_SCOPE pasted the literal __LINE__ into the identifier.
    auto& prof = optigd::profiling::Profiler::instance();
    prof.reset();
    {
        OPTIGD_PROFILE_SCOPE("alpha");
        OPTIGD_PROFILE_SCOPE("beta");
    }
    const auto alpha = prof.getMetric("alpha");
    const auto beta = prof.getMetric("beta");
    TEST_ASSERT(alpha.callCount == 1, "alpha scope must record once");
    TEST_ASSERT(beta.callCount == 1, "beta scope must record once");
    return true;
}

bool testSpikeThresholdFollowsTargetFps() {
    auto& diag = optigd::diagnostics::DiagnosticsEngine::instance();
    diag.reset();
    adp::AdaptiveGovernor gov(core::OptiConfig{});
    adp::FallbackManager fb(4.0f, 10);
    diag.setTargetFrameRate(60.0f);
    TEST_ASSERT(std::abs(diag.spikeThresholdUs() - 2.0e6 / 60.0) < 1e-6, "60 fps threshold wrong");

    diag.setTargetFrameRate(30.0f);
    TEST_ASSERT(std::abs(diag.spikeThresholdUs() - 2.0e6 / 30.0) < 1e-6, "30 fps threshold wrong");

    diag.setTargetFrameRate(-5.0f); // invalid input keeps the previous threshold
    TEST_ASSERT(std::abs(diag.spikeThresholdUs() - 2.0e6 / 30.0) < 1e-6, "invalid fps must be ignored");

    core::FrameStats f{};
    f.frameIntervalUs = 50000.0; // 50 ms: spike at 60 fps, not at 30 fps
    diag.recordFrame(f);
    TEST_ASSERT(diag.generateSummary(gov, fb).totalSpikes == 0,
                "50 ms must not be a spike at 30 fps");

    diag.setTargetFrameRate(60.0f);
    diag.recordFrame(f);
    TEST_ASSERT(diag.generateSummary(gov, fb).totalSpikes == 1,
                "50 ms must be a spike at 60 fps");
    diag.setTargetFrameRate(60.0f);
    diag.reset();
    return true;
}

bool testDiagnosticsSummaryFromIntervals() {
    auto& diag = optigd::diagnostics::DiagnosticsEngine::instance();
    diag.reset();
    adp::AdaptiveGovernor gov(core::OptiConfig{});
    adp::FallbackManager fb(4.0f, 10);

    for (int i = 0; i < 10; ++i) {
        core::FrameStats f{};
        f.frameIndex = static_cast<uint64_t>(i);
        f.frameIntervalUs = (i == 0) ? 0.0 : 20000.0; // first frame has no interval
        f.gameUpdateUs = 1000.0;
        f.optigdOverheadUs = 10.0;
        f.totalObjects = 100;
        f.shadowCulledObjects = 25;
        f.arenaAllocations = 3;
        diag.recordFrame(f);
    }
    const auto s = diag.generateSummary(gov, fb);
    TEST_ASSERT(s.totalFrames == 10, "all frames counted");
    TEST_ASSERT(s.measuredIntervals == 9, "zero interval must be excluded");
    TEST_ASSERT(std::abs(s.meanFrameIntervalMs - 20.0) < 1e-9, "mean interval wrong");
    TEST_ASSERT(std::abs(s.averageFps - 50.0) < 1e-9, "fps wrong");
    TEST_ASSERT(std::abs(s.shadowCullRatio - 0.25) < 1e-9, "shadow ratio wrong");
    TEST_ASSERT(s.totalArenaAllocations == 30, "arena allocations must sum");
    TEST_ASSERT(s.totalSpikes == 0, "20 ms frames are not spikes");

    core::FrameStats spike{};
    spike.frameIntervalUs = 50000.0;
    diag.recordFrame(spike);
    TEST_ASSERT(diag.generateSummary(gov, fb).totalSpikes == 1, "50 ms frame must count as a spike");
    diag.reset();
    return true;
}

bool testFrameTimeRecorderWritesCsv() {
    const auto path = std::filesystem::temp_directory_path() / "optigd_test_frametimes.csv";
    optigd::diagnostics::FrameTimeRecorder rec;
    TEST_ASSERT(rec.open(path.string()), "recorder open failed");
    core::FrameStats f{};
    f.frameIndex = 42;
    f.frameIntervalUs = 16666.5;
    rec.append(f, "LowEnd");
    rec.close();

    std::ifstream in(path);
    std::string header, row;
    std::getline(in, header);
    std::getline(in, row);
    in.close();
    std::filesystem::remove(path);

    TEST_ASSERT(header == optigd::diagnostics::FrameTimeRecorder::kHeader, "CSV header mismatch");
    TEST_ASSERT(row.rfind("42,16666.5,", 0) == 0, "CSV row must start with frame index and interval");
    TEST_ASSERT(row.size() >= 5 && row.substr(row.size() - 7) == ",LowEnd", "CSV row must end with tier");
    return true;
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

struct NamedTest {
    const char* name;
    std::function<bool()> fn;
};

int main() {
    std::cout << "\n=========================================\n";
    std::cout << "   OPTIGD v0.1.0-beta1 NATIVE UNIT & REGRESSION TESTS\n";
    std::cout << "=========================================\n\n";

    const std::vector<NamedTest> tests = {
        {"MemoryArena basics", testMemoryArenaBasics},
        {"MemoryArena rejects bad alignment", testMemoryArenaRejectsNonPowerOfTwoAlignment},
        {"MemoryArena rejects overflowing size", testMemoryArenaRejectsOverflowingSize},
        {"SpatialHash query", testSpatialHashQuery},
        {"SpatialHash moved item", testSpatialHashMovedItemIsFoundAtNewLocation},
        {"SpatialHash remove and negative coords", testSpatialHashRemoveAndNegativeCoordinates},
        {"SpatialHash rejects non-finite input", testSpatialHashRejectsNonFiniteInput},
        {"VisibilityCuller counts", testVisibilityCullerCounts},
        {"VisibilityCuller update and unregister", testVisibilityCullerUpdateAndUnregister},
        {"ParticleGovernor original behaviour", testParticleGovernorOriginalBehaviour},
        {"ParticleGovernor critical never throttled", testParticleGovernorCriticalNeverThrottled},
        {"ParticleGovernor hysteresis band", testParticleGovernorHysteresisBand},
        {"ParticleGovernor wake has no catch-up", testParticleGovernorWakeHasNoCatchUp},
        {"ParticleGovernor incremental totals", testParticleGovernorIncrementalTotals},
        {"ParticleGovernor budget multiplier", testParticleGovernorBudgetReducesMultiplier},
        {"TriggerOptimizer original behaviour", testTriggerOptimizerOriginalBehaviour},
        {"TriggerOptimizer re-arms on restart", testTriggerOptimizerRearmsOnRestart},
        {"TriggerOptimizer requires prepare after registration", testTriggerOptimizerRequiresPrepareAfterRegistration},
        {"TriggerOptimizer spawn groups", testTriggerOptimizerSpawnGroups},
        {"MatrixCache dirty invalidates", testMatrixCacheMarkDirtyInvalidates},
        {"MatrixCache input change misses", testMatrixCacheInputChangeMisses},
        {"Affine concat composition", testAffineConcatComposition},
        {"AdaptiveGovernor original behaviour", testAdaptiveGovernorOriginalBehaviour},
        {"AdaptiveGovernor never mutates user config", testAdaptiveGovernorNeverMutatesUserConfig},
        {"AdaptiveGovernor ignores invalid samples and Custom", testAdaptiveGovernorIgnoresInvalidSamplesAndCustom},
        {"FallbackManager escalates and recovers", testFallbackEscalatesAndRecovers},
        {"FallbackManager user override", testFallbackUserOverride},
        {"ConfigManager round trip", testConfigSerializationRoundTrip},
        {"ConfigManager rejects garbage", testConfigDeserializeRejectsGarbageWithoutThrowing},
        {"Presets and tier parsing", testPresetsAndTierParsing},
        {"SettingsMapping unknown tier warns", testSettingsMappingUnknownTierWarns},
        {"SettingsMapping flags and clamps", testSettingsMappingFlagsAndCustomClamp},
        {"Default flags match decision", testDefaultFlagsMatchDecision},
        {"Version strings follow Geode grammar", testVersionStringsFollowGeodeGrammar},
        {"CompatibilityLayer conflict and defaults", testCompatibilityConflictAndDefaults},
        {"Profiler scopes do not collide", testProfilerScopesDoNotCollide},
        {"Diagnostics summary from intervals", testDiagnosticsSummaryFromIntervals},
        {"Spike threshold follows target FPS", testSpikeThresholdFollowsTargetFps},
        {"FrameTimeRecorder writes CSV", testFrameTimeRecorderWritesCsv},
    };

    size_t passed = 0;
    for (const auto& t : tests) {
        std::cout << "  " << t.name << " ... ";
        if (t.fn()) {
            std::cout << "ok\n";
            passed++;
        } else {
            std::cout << "FAILED\n";
        }
    }

    std::cout << "\n=========================================\n";
    std::cout << "   " << passed << "/" << tests.size() << " tests passed\n";
    std::cout << "=========================================\n\n";
    return passed == tests.size() ? 0 : 1;
}

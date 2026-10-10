#pragma once

#include "../core/Types.hpp"
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace optigd::optimization {

enum class TriggerType : uint8_t {
    TouchOrPosition = 0, // one-shot when the player's X reaches triggerX (MODEL, see below)
    Spawn = 1,           // invoked explicitly by group id
    Continuous = 2       // evaluated every call (MODEL placeholder)
};

struct OptimizedTrigger {
    uint32_t id{0};
    uint32_t groupID{0};
    TriggerType type{TriggerType::TouchOrPosition};
    float triggerX{0.0f};
    bool activated{false};
    bool isCompleted{false};
    void* nativePtr{nullptr};
};

// Ordered index over level triggers.
//
// IMPORTANT — this is a model, not Geometry Dash's trigger system. Real GD triggers
// are EffectGameObjects activated through collision checks (checkCollisions) and
// timed command/move processing (processCommands, processMoveActionsStep). This
// class does not reproduce those semantics. It is therefore DISABLED in
// v0.1.0-beta1 and must not be connected to gameplay without an in-engine
// equivalence study. Its tests verify the index mechanics only.
//
// Lifecycle:
//  - registerTrigger() may be called at any time; it invalidates the prepared index.
//  - prepareLevel() (re)builds the index and re-arms every trigger. Calling it on a
//    level restart is required: triggers must fire again after a reset.
//  - evaluate()/invokeSpawnGroup() return nothing until prepareLevel() succeeded.
class TriggerOptimizer {
public:
    explicit TriggerOptimizer(float lookaheadDistance = 800.0f);

    void setLookaheadDistance(float distance);
    float getLookaheadDistance() const { return m_lookaheadDistance; }

    void clear();
    void registerTrigger(uint32_t id, uint32_t groupID, TriggerType type, float triggerX, void* nativePtr);
    bool prepareLevel();
    bool isPrepared() const { return m_prepared; }

    // Appends the native pointers of triggers to execute for this player X position.
    size_t evaluate(float playerX, std::vector<void*>& outTriggersToExecute);

    // Appends every trigger in the given spawn group.
    size_t invokeSpawnGroup(uint32_t targetGroupID, std::vector<void*>& outTriggersToExecute);

    size_t getTotalTriggers() const { return m_triggers.size(); }
    size_t getLastEvaluatedCount() const { return m_lastEvaluatedCount; }
    size_t getLastExecutedCount() const { return m_lastExecutedCount; }

private:
    float m_lookaheadDistance{800.0f};
    bool m_prepared{false};
    std::vector<OptimizedTrigger> m_triggers;
    std::vector<OptimizedTrigger*> m_sortedXTriggers;
    std::vector<OptimizedTrigger*> m_continuousTriggers;
    std::unordered_map<uint32_t, std::vector<OptimizedTrigger*>> m_spawnGroups;

    size_t m_cursorIndex{0};
    size_t m_lastEvaluatedCount{0};
    size_t m_lastExecutedCount{0};
};

} // namespace optigd::optimization

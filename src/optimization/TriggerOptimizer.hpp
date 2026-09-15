#pragma once

#include "../core/Types.hpp"
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <algorithm>

namespace optigd::optimization {

enum class TriggerType : uint8_t {
    TouchOrPosition = 0, // Activated when player crosses X coordinate
    Spawn = 1,           // Activated via group ID invocation
    Continuous = 2       // Evaluated every frame (e.g. follow player, rotate)
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

class TriggerOptimizer {
public:
    explicit TriggerOptimizer(float lookaheadDistance = 800.0f);

    void setLookaheadDistance(float distance);
    float getLookaheadDistance() const { return m_lookaheadDistance; }

    void clear();
    void registerTrigger(uint32_t id, uint32_t groupID, TriggerType type, float triggerX, void* nativePtr);
    void prepareLevel(); // Sorts and indexes triggers

    // Evaluates triggers relevant for current player X position and window
    // Fills outTriggersToExecute with pointer to active triggers
    size_t evaluate(float playerX, std::vector<void*>& outTriggersToExecute);

    // Instant O(1) group spawn invocation
    void invokeSpawnGroup(uint32_t targetGroupID, std::vector<void*>& outTriggersToExecute);

    size_t getTotalTriggers() const { return m_triggers.size(); }
    size_t getLastEvaluatedCount() const { return m_lastEvaluatedCount; }
    size_t getLastExecutedCount() const { return m_lastExecutedCount; }

private:
    float m_lookaheadDistance{800.0f};
    std::vector<OptimizedTrigger> m_triggers;
    std::vector<OptimizedTrigger*> m_sortedXTriggers;
    std::vector<OptimizedTrigger*> m_continuousTriggers;
    std::unordered_map<uint32_t, std::vector<OptimizedTrigger*>> m_spawnGroups;

    size_t m_cursorIndex{0};
    size_t m_lastEvaluatedCount{0};
    size_t m_lastExecutedCount{0};
};

} // namespace optigd::optimization

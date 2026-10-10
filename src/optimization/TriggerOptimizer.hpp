#pragma once

#include "../core/Types.hpp"
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace optigd::optimization {

enum class TriggerType : uint8_t {
    TouchOrPosition = 0, // Activated when player crosses X coordinate
    Spawn = 1,           // Activated via group ID invocation
    Continuous = 2,      // Evaluated every frame (e.g. follow player, rotate)
    TouchOnly = 3        // Activated only when player hitbox intersects trigger bounds
};

struct OptimizedTrigger {
    uint32_t id{0};
    uint32_t groupID{0};
    TriggerType type{TriggerType::TouchOrPosition};
    float triggerX{0.0f};
    core::Rect2D bounds{};
    int32_t spawnOrder{0};
    bool isMultiTriggered{false};
    bool enabled{true};
    bool activated{false};
    bool isCompleted{false};
    void* nativePtr{nullptr};
    // Bookkeeping for inspectLookaheadWindow(). Kept separate from `activated`
    // so telemetry never perturbs the execution state used by evaluate() and
    // evaluateWithHitbox(). Declared last so that adding it cannot shift any
    // positional aggregate initialiser.
    bool reportedInLookahead{false};
};

class TriggerOptimizer {
public:
    explicit TriggerOptimizer(float lookaheadDistance = 800.0f);

    void setLookaheadDistance(float distance);
    float getLookaheadDistance() const { return m_lookaheadDistance; }

    void clear();
    void resetTriggerStates();

    void registerTrigger(uint32_t id, uint32_t groupID, TriggerType type, float triggerX,
                         void* nativePtr, bool isMultiTriggered = false,
                         int32_t spawnOrder = 0, const core::Rect2D& bounds = {});

    void prepareLevel(); // Sorts and indexes triggers deterministically

    // Evaluates triggers relevant for current player X position and window
    size_t evaluate(float playerX, std::vector<void*>& outTriggersToExecute);

    // Evaluates position, continuous, and touch-hitbox triggers
    size_t evaluateWithHitbox(float playerX, const core::Rect2D& playerHitbox,
                              std::vector<void*>& outTriggersToExecute);

    // Lookahead window inspection for safe gameplay telemetry.
    //
    // Advances the internal scan cursor (that is what keeps the scan
    // amortised O(1)) but never mutates `activated` / `isCompleted`, so it
    // cannot change what evaluate() or evaluateWithHitbox() would execute.
    size_t inspectLookaheadWindow(float playerX);

    // Instant O(1) group spawn invocation
    void invokeSpawnGroup(uint32_t targetGroupID, std::vector<void*>& outTriggersToExecute);

    // Toggle group enabled state (mirrors GD Toggle Trigger)
    void setGroupEnabled(uint32_t targetGroupID, bool enabled);
    bool isGroupEnabled(uint32_t targetGroupID) const;

    void rewindCursorTo(float xPosition);

    size_t getTotalTriggers() const { return m_triggers.size(); }
    size_t getLastEvaluatedCount() const { return m_lastEvaluatedCount; }
    size_t getLastExecutedCount() const { return m_lastExecutedCount; }
    size_t getCursorIndex() const { return m_cursorIndex; }

private:
    float m_lookaheadDistance{800.0f};
    std::vector<OptimizedTrigger> m_triggers;
    std::vector<OptimizedTrigger*> m_sortedXTriggers;
    std::vector<OptimizedTrigger*> m_continuousTriggers;
    std::unordered_map<uint32_t, std::vector<OptimizedTrigger*>> m_spawnGroups;
    std::unordered_map<uint32_t, bool> m_disabledGroups;

    size_t m_cursorIndex{0};
    float m_lastPlayerX{0.0f};
    bool m_hasEvaluated{false};
    size_t m_lastEvaluatedCount{0};
    size_t m_lastExecutedCount{0};

    static bool compareTriggersDeterministic(const OptimizedTrigger* a, const OptimizedTrigger* b);
};

} // namespace optigd::optimization

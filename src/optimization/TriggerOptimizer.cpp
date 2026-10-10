#include "TriggerOptimizer.hpp"

#include <algorithm>
#include <cmath>

namespace optigd::optimization {

TriggerOptimizer::TriggerOptimizer(float lookaheadDistance)
    : m_lookaheadDistance(std::isfinite(lookaheadDistance) ? std::max(0.0f, lookaheadDistance) : 800.0f) {}

void TriggerOptimizer::setLookaheadDistance(float distance) {
    if (std::isfinite(distance)) {
        m_lookaheadDistance = std::max(0.0f, distance);
    }
}

void TriggerOptimizer::clear() {
    m_triggers.clear();
    m_sortedXTriggers.clear();
    m_continuousTriggers.clear();
    m_spawnGroups.clear();
    m_prepared = false;
    m_cursorIndex = 0;
    m_lastEvaluatedCount = 0;
    m_lastExecutedCount = 0;
}

void TriggerOptimizer::registerTrigger(uint32_t id, uint32_t groupID, TriggerType type, float triggerX, void* nativePtr) {
    if (!std::isfinite(triggerX)) return;
    m_triggers.push_back({id, groupID, type, triggerX, false, false, nativePtr});
    // Pointers into m_triggers are no longer valid for the prepared index.
    m_prepared = false;
}

bool TriggerOptimizer::prepareLevel() {
    m_sortedXTriggers.clear();
    m_continuousTriggers.clear();
    m_spawnGroups.clear();
    m_cursorIndex = 0;
    m_lastEvaluatedCount = 0;
    m_lastExecutedCount = 0;

    for (auto& trig : m_triggers) {
        // Re-arm: a restart must allow every trigger to fire again.
        trig.activated = false;
        trig.isCompleted = false;

        if (trig.type == TriggerType::TouchOrPosition) {
            m_sortedXTriggers.push_back(&trig);
        } else if (trig.type == TriggerType::Continuous) {
            m_continuousTriggers.push_back(&trig);
        }

        if (trig.groupID != 0) {
            m_spawnGroups[trig.groupID].push_back(&trig);
        }
    }

    std::stable_sort(m_sortedXTriggers.begin(), m_sortedXTriggers.end(),
                     [](const OptimizedTrigger* a, const OptimizedTrigger* b) {
                         return a->triggerX < b->triggerX;
                     });
    m_prepared = true;
    return true;
}

size_t TriggerOptimizer::evaluate(float playerX, std::vector<void*>& outTriggersToExecute) {
    outTriggersToExecute.clear();
    m_lastEvaluatedCount = 0;
    m_lastExecutedCount = 0;
    if (!m_prepared || !std::isfinite(playerX)) {
        return 0;
    }

    // 1. Continuous (model placeholder): evaluated on every call.
    for (auto* trig : m_continuousTriggers) {
        m_lastEvaluatedCount++;
        outTriggersToExecute.push_back(trig->nativePtr);
        m_lastExecutedCount++;
    }

    // 2. Advance the cursor past completed triggers that are well behind the player.
    //    Only completed triggers are skipped, so an un-fired trigger is never passed over.
    const float backwardTolerance = 50.0f;
    while (m_cursorIndex < m_sortedXTriggers.size() &&
           m_sortedXTriggers[m_cursorIndex]->triggerX < (playerX - backwardTolerance) &&
           m_sortedXTriggers[m_cursorIndex]->isCompleted) {
        m_cursorIndex++;
    }

    // 3. Scan from the cursor up to playerX + lookahead.
    const float maxX = playerX + m_lookaheadDistance;
    for (size_t i = m_cursorIndex; i < m_sortedXTriggers.size(); ++i) {
        auto* trig = m_sortedXTriggers[i];
        if (trig->triggerX > maxX) {
            break; // sorted: nothing further can be inside the window
        }

        m_lastEvaluatedCount++;
        if (!trig->activated && playerX >= trig->triggerX) {
            trig->activated = true;
            trig->isCompleted = true;
            outTriggersToExecute.push_back(trig->nativePtr);
            m_lastExecutedCount++;
        }
    }

    return m_lastExecutedCount;
}

size_t TriggerOptimizer::invokeSpawnGroup(uint32_t targetGroupID, std::vector<void*>& outTriggersToExecute) {
    size_t executed = 0;
    if (!m_prepared) return 0;
    auto it = m_spawnGroups.find(targetGroupID);
    if (it != m_spawnGroups.end()) {
        for (auto* trig : it->second) {
            outTriggersToExecute.push_back(trig->nativePtr);
            executed++;
        }
    }
    return executed;
}

} // namespace optigd::optimization

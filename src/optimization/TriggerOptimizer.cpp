#include "TriggerOptimizer.hpp"

namespace optigd::optimization {

TriggerOptimizer::TriggerOptimizer(float lookaheadDistance)
    : m_lookaheadDistance(lookaheadDistance) {}

void TriggerOptimizer::setLookaheadDistance(float distance) {
    m_lookaheadDistance = distance;
}

void TriggerOptimizer::clear() {
    m_triggers.clear();
    m_sortedXTriggers.clear();
    m_continuousTriggers.clear();
    m_spawnGroups.clear();
    m_cursorIndex = 0;
    m_lastEvaluatedCount = 0;
    m_lastExecutedCount = 0;
}

void TriggerOptimizer::registerTrigger(uint32_t id, uint32_t groupID, TriggerType type, float triggerX, void* nativePtr) {
    m_triggers.push_back({id, groupID, type, triggerX, false, false, nativePtr});
}

void TriggerOptimizer::prepareLevel() {
    m_sortedXTriggers.clear();
    m_continuousTriggers.clear();
    m_spawnGroups.clear();
    m_cursorIndex = 0;

    for (auto& trig : m_triggers) {
        if (trig.type == TriggerType::TouchOrPosition) {
            m_sortedXTriggers.push_back(&trig);
        } else if (trig.type == TriggerType::Continuous) {
            m_continuousTriggers.push_back(&trig);
        }

        if (trig.groupID != 0) {
            m_spawnGroups[trig.groupID].push_back(&trig);
        }
    }

    std::sort(m_sortedXTriggers.begin(), m_sortedXTriggers.end(),
              [](const OptimizedTrigger* a, const OptimizedTrigger* b) {
                  return a->triggerX < b->triggerX;
              });
}

size_t TriggerOptimizer::evaluate(float playerX, std::vector<void*>& outTriggersToExecute) {
    outTriggersToExecute.clear();
    m_lastEvaluatedCount = 0;
    m_lastExecutedCount = 0;

    // 1. Continuous triggers always evaluate
    for (auto* trig : m_continuousTriggers) {
        m_lastEvaluatedCount++;
        outTriggersToExecute.push_back(trig->nativePtr);
        m_lastExecutedCount++;
    }

    // 2. Advance cursor past triggers far behind player
    const float backwardTolerance = 50.0f;
    while (m_cursorIndex < m_sortedXTriggers.size() &&
           m_sortedXTriggers[m_cursorIndex]->triggerX < (playerX - backwardTolerance) &&
           m_sortedXTriggers[m_cursorIndex]->isCompleted) {
        m_cursorIndex++;
    }

    // 3. Scan from cursorIndex until playerX + lookaheadDistance
    float maxX = playerX + m_lookaheadDistance;
    for (size_t i = m_cursorIndex; i < m_sortedXTriggers.size(); ++i) {
        auto* trig = m_sortedXTriggers[i];
        if (trig->triggerX > maxX) {
            // Reached triggers beyond lookahead: stop scanning early!
            break;
        }

        m_lastEvaluatedCount++;

        // Trigger executes if player has reached or passed its X position and not yet activated
        if (!trig->activated && playerX >= trig->triggerX) {
            trig->activated = true;
            trig->isCompleted = true; // One-shot trigger completed
            outTriggersToExecute.push_back(trig->nativePtr);
            m_lastExecutedCount++;
        }
    }

    return m_lastExecutedCount;
}

void TriggerOptimizer::invokeSpawnGroup(uint32_t targetGroupID, std::vector<void*>& outTriggersToExecute) {
    auto it = m_spawnGroups.find(targetGroupID);
    if (it != m_spawnGroups.end()) {
        for (auto* trig : it->second) {
            outTriggersToExecute.push_back(trig->nativePtr);
            m_lastExecutedCount++;
        }
    }
}

} // namespace optigd::optimization

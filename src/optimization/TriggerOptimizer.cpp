#include "TriggerOptimizer.hpp"
#include <algorithm>
#include <cmath>

namespace optigd::optimization {

TriggerOptimizer::TriggerOptimizer(float lookaheadDistance)
    : m_lookaheadDistance(std::max(50.0f, lookaheadDistance)) {}

void TriggerOptimizer::setLookaheadDistance(float distance) {
    if (std::isfinite(distance) && distance > 0.0f) {
        m_lookaheadDistance = distance;
    }
}

void TriggerOptimizer::clear() {
    m_triggers.clear();
    m_sortedXTriggers.clear();
    m_continuousTriggers.clear();
    m_spawnGroups.clear();
    m_disabledGroups.clear();
    m_cursorIndex = 0;
    m_lastPlayerX = 0.0f;
    m_hasEvaluated = false;
    m_lastEvaluatedCount = 0;
    m_lastExecutedCount = 0;
}

void TriggerOptimizer::resetTriggerStates() {
    for (auto& trig : m_triggers) {
        trig.activated = false;
        trig.isCompleted = false;
        trig.enabled = true;
    }
    m_disabledGroups.clear();
    m_cursorIndex = 0;
    m_lastPlayerX = 0.0f;
    m_hasEvaluated = false;
    m_lastEvaluatedCount = 0;
    m_lastExecutedCount = 0;
}

void TriggerOptimizer::registerTrigger(uint32_t id, uint32_t groupID, TriggerType type, float triggerX,
                                       void* nativePtr, bool isMultiTriggered,
                                       int32_t spawnOrder, const core::Rect2D& bounds) {
    core::Rect2D effectiveBounds = bounds;
    if (effectiveBounds.size.width <= 0.0f && effectiveBounds.size.height <= 0.0f) {
        effectiveBounds = core::Rect2D(triggerX - 15.0f, 0.0f, 30.0f, 2000.0f);
    }
    m_triggers.push_back({
        id,
        groupID,
        type,
        triggerX,
        effectiveBounds.normalized(),
        spawnOrder,
        isMultiTriggered,
        true,
        false,
        false,
        nativePtr
    });
}

bool TriggerOptimizer::compareTriggersDeterministic(const OptimizedTrigger* a, const OptimizedTrigger* b) {
    if (a->triggerX != b->triggerX) {
        return a->triggerX < b->triggerX;
    }
    if (a->spawnOrder != b->spawnOrder) {
        return a->spawnOrder < b->spawnOrder;
    }
    return a->id < b->id;
}

void TriggerOptimizer::prepareLevel() {
    m_sortedXTriggers.clear();
    m_continuousTriggers.clear();
    m_spawnGroups.clear();
    m_disabledGroups.clear();
    m_cursorIndex = 0;
    m_lastPlayerX = 0.0f;
    m_hasEvaluated = false;

    for (auto& trig : m_triggers) {
        trig.activated = false;
        trig.isCompleted = false;
        trig.enabled = true;

        if (trig.type == TriggerType::TouchOrPosition || trig.type == TriggerType::TouchOnly) {
            m_sortedXTriggers.push_back(&trig);
        } else if (trig.type == TriggerType::Continuous) {
            m_continuousTriggers.push_back(&trig);
        }

        if (trig.groupID != 0) {
            m_spawnGroups[trig.groupID].push_back(&trig);
        }
    }

    std::stable_sort(m_sortedXTriggers.begin(), m_sortedXTriggers.end(), compareTriggersDeterministic);
    for (auto& [_, groupList] : m_spawnGroups) {
        std::stable_sort(groupList.begin(), groupList.end(), compareTriggersDeterministic);
    }
}

void TriggerOptimizer::rewindCursorTo(float xPosition) {
    if (m_sortedXTriggers.empty()) {
        m_cursorIndex = 0;
        return;
    }
    const float backwardTolerance = 50.0f;
    float targetX = xPosition - backwardTolerance;

    auto it = std::lower_bound(
        m_sortedXTriggers.begin(),
        m_sortedXTriggers.end(),
        targetX,
        [](const OptimizedTrigger* trig, float x) {
            return trig->triggerX < x;
        }
    );
    m_cursorIndex = static_cast<size_t>(std::distance(m_sortedXTriggers.begin(), it));

    // Re-arm multi-triggers ahead of the rewound position
    for (size_t i = m_cursorIndex; i < m_sortedXTriggers.size(); ++i) {
        auto* trig = m_sortedXTriggers[i];
        if (trig->isMultiTriggered && trig->triggerX > xPosition) {
            trig->activated = false;
            trig->isCompleted = false;
        }
    }
}

size_t TriggerOptimizer::evaluate(float playerX, std::vector<void*>& outTriggersToExecute) {
    return evaluateWithHitbox(playerX, core::Rect2D(playerX - 15.0f, -1e6f, 30.0f, 2e6f), outTriggersToExecute);
}

size_t TriggerOptimizer::evaluateWithHitbox(float playerX, const core::Rect2D& playerHitbox,
                                            std::vector<void*>& outTriggersToExecute) {
    outTriggersToExecute.clear();
    m_lastEvaluatedCount = 0;
    m_lastExecutedCount = 0;

    // Detect backward movement (platformer mode, reverse portal, checkpoint restore)
    if (m_hasEvaluated && playerX < m_lastPlayerX) {
        rewindCursorTo(playerX);
    }
    m_lastPlayerX = playerX;
    m_hasEvaluated = true;

    // 1. Continuous triggers always evaluate if enabled
    for (auto* trig : m_continuousTriggers) {
        if (!trig->enabled) continue;
        m_lastEvaluatedCount++;
        outTriggersToExecute.push_back(trig->nativePtr);
        m_lastExecutedCount++;
    }

    // 2. Advance cursor past completed one-shot triggers far behind player
    const float backwardTolerance = 50.0f;
    while (m_cursorIndex < m_sortedXTriggers.size() &&
           m_sortedXTriggers[m_cursorIndex]->triggerX < (playerX - backwardTolerance) &&
           m_sortedXTriggers[m_cursorIndex]->isCompleted &&
           !m_sortedXTriggers[m_cursorIndex]->isMultiTriggered) {
        m_cursorIndex++;
    }

    // 3. Scan from cursorIndex until playerX + lookaheadDistance
    float maxX = playerX + m_lookaheadDistance;
    core::Rect2D normHitbox = playerHitbox.normalized();

    for (size_t i = m_cursorIndex; i < m_sortedXTriggers.size(); ++i) {
        auto* trig = m_sortedXTriggers[i];
        if (trig->triggerX > maxX) {
            break;
        }

        if (!trig->enabled) {
            continue;
        }

        m_lastEvaluatedCount++;

        if (trig->type == TriggerType::TouchOnly) {
            bool touching = trig->bounds.intersects(normHitbox);
            if (touching && !trig->activated) {
                trig->activated = true;
                if (!trig->isMultiTriggered) {
                    trig->isCompleted = true;
                }
                outTriggersToExecute.push_back(trig->nativePtr);
                m_lastExecutedCount++;
            } else if (!touching && trig->isMultiTriggered) {
                // Re-arm multi-trigger once player leaves touch bounds
                trig->activated = false;
            }
        } else if (trig->type == TriggerType::TouchOrPosition) {
            if (!trig->activated && playerX >= trig->triggerX) {
                trig->activated = true;
                if (!trig->isMultiTriggered) {
                    trig->isCompleted = true;
                }
                outTriggersToExecute.push_back(trig->nativePtr);
                m_lastExecutedCount++;
            } else if (trig->isMultiTriggered && playerX < trig->triggerX) {
                trig->activated = false;
            }
        }
    }

    return m_lastExecutedCount;
}

size_t TriggerOptimizer::inspectLookaheadWindow(float playerX) {
    m_lastEvaluatedCount = m_continuousTriggers.size();
    m_lastExecutedCount = 0;

    if (m_sortedXTriggers.empty()) {
        return m_lastEvaluatedCount;
    }

    if (m_hasEvaluated && playerX < m_lastPlayerX) {
        rewindCursorTo(playerX);
    }
    m_lastPlayerX = playerX;
    m_hasEvaluated = true;

    const float backwardTolerance = 50.0f;
    while (m_cursorIndex < m_sortedXTriggers.size() &&
           m_sortedXTriggers[m_cursorIndex]->triggerX < (playerX - backwardTolerance)) {
        m_cursorIndex++;
    }

    float maxX = playerX + m_lookaheadDistance;
    for (size_t i = m_cursorIndex; i < m_sortedXTriggers.size(); ++i) {
        auto* trig = m_sortedXTriggers[i];
        if (trig->triggerX > maxX) {
            break;
        }
        if (trig->enabled) {
            m_lastEvaluatedCount++;
            if (playerX >= trig->triggerX && !trig->activated) {
                trig->activated = true;
                m_lastExecutedCount++;
            }
        }
    }
    return m_lastEvaluatedCount;
}

void TriggerOptimizer::invokeSpawnGroup(uint32_t targetGroupID, std::vector<void*>& outTriggersToExecute) {
    if (!isGroupEnabled(targetGroupID)) {
        return;
    }
    auto it = m_spawnGroups.find(targetGroupID);
    if (it != m_spawnGroups.end()) {
        for (auto* trig : it->second) {
            if (!trig->enabled) continue;
            if (trig->isCompleted && !trig->isMultiTriggered && trig->type != TriggerType::Spawn) {
                continue;
            }
            trig->activated = true;
            if (!trig->isMultiTriggered && trig->type != TriggerType::Spawn) {
                trig->isCompleted = true;
            }
            outTriggersToExecute.push_back(trig->nativePtr);
            m_lastExecutedCount++;
        }
    }
}

void TriggerOptimizer::setGroupEnabled(uint32_t targetGroupID, bool enabled) {
    if (targetGroupID == 0) return;
    if (enabled) {
        m_disabledGroups.erase(targetGroupID);
    } else {
        m_disabledGroups[targetGroupID] = true;
    }

    auto it = m_spawnGroups.find(targetGroupID);
    if (it != m_spawnGroups.end()) {
        for (auto* trig : it->second) {
            trig->enabled = enabled;
        }
    }
}

bool TriggerOptimizer::isGroupEnabled(uint32_t targetGroupID) const {
    if (targetGroupID == 0) return true;
    return m_disabledGroups.find(targetGroupID) == m_disabledGroups.end();
}

} // namespace optigd::optimization

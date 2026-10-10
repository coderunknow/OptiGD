#include "VisibilityCuller.hpp"
#include <cmath>

namespace optigd::optimization {

VisibilityCuller::VisibilityCuller(float cellSize)
    : m_spatialHash(cellSize) {}

void VisibilityCuller::setCellSize(float cellSize) {
    m_spatialHash.setCellSize(cellSize);
    m_cameraCached = false;
}

void VisibilityCuller::setPadding(float padX, float padY) {
    m_padX = std::max(0.0f, padX);
    m_padY = std::max(0.0f, padY);
    m_cameraCached = false;
}

void VisibilityCuller::clear() {
    m_spatialHash.clear();
    m_objects.clear();
    m_idToIndex.clear();
    m_ptrToIndex.clear();
    m_previouslyVisibleIndices.clear();
    m_activeObjectCount = 0;
    m_currentFrameStamp = 1;
    m_lastCulledCount = 0;
    m_lastVisibleCount = 0;
    m_lastNewlyVisibleCount = 0;
    m_lastNewlyCulledCount = 0;
    m_cameraCached = false;
}

void VisibilityCuller::reserve(size_t capacity) {
    m_spatialHash.reserve(capacity);
    m_objects.reserve(capacity);
    m_idToIndex.reserve(capacity);
    m_ptrToIndex.reserve(capacity);
}

uint32_t VisibilityCuller::registerObject(uint32_t id, const core::Rect2D& bounds, void* nativePtr,
                                          bool isStatic, bool isDecorative) {
    if (nativePtr != nullptr) {
        auto existingIt = m_ptrToIndex.find(nativePtr);
        if (existingIt != m_ptrToIndex.end()) {
            uint32_t idx = existingIt->second;
            updateObjectBounds(m_objects[idx].id, bounds);
            return m_objects[idx].id;
        }
    }

    uint32_t objIndex = static_cast<uint32_t>(m_objects.size());
    uint32_t spatialId = m_spatialHash.insert(bounds, reinterpret_cast<void*>(static_cast<uintptr_t>(objIndex)));

    RenderObject obj{};
    obj.id = id;
    obj.spatialId = spatialId;
    obj.bounds = bounds.normalized();
    obj.nativePtr = nativePtr;
    obj.isVisible = true;
    obj.isStatic = isStatic;
    obj.isDecorative = isDecorative;
    obj.active = true;
    obj.lastSeenStamp = 0;

    m_objects.push_back(obj);
    m_idToIndex[id] = objIndex;
    if (nativePtr != nullptr) {
        m_ptrToIndex[nativePtr] = objIndex;
    }
    m_activeObjectCount++;
    m_cameraCached = false;
    return id;
}

void VisibilityCuller::updateObjectBounds(uint32_t id, const core::Rect2D& newBounds) {
    uint32_t idx = id;
    if (idx >= m_objects.size() || m_objects[idx].id != id) {
        auto it = m_idToIndex.find(id);
        if (it == m_idToIndex.end()) return;
        idx = it->second;
    }
    auto& obj = m_objects[idx];
    if (!obj.active) return;

    obj.bounds = newBounds.normalized();
    m_spatialHash.update(obj.spatialId, obj.bounds);
    m_cameraCached = false;
}

bool VisibilityCuller::updateObjectBoundsByPtr(void* nativePtr, const core::Rect2D& newBounds) {
    if (!nativePtr) return false;
    auto it = m_ptrToIndex.find(nativePtr);
    if (it == m_ptrToIndex.end()) return false;

    auto& obj = m_objects[it->second];
    if (!obj.active) return false;

    obj.bounds = newBounds.normalized();
    m_spatialHash.update(obj.spatialId, obj.bounds);
    m_cameraCached = false;
    return true;
}

bool VisibilityCuller::unregisterObject(uint32_t id) {
    auto it = m_idToIndex.find(id);
    if (it == m_idToIndex.end()) return false;

    uint32_t idx = it->second;
    auto& obj = m_objects[idx];
    if (!obj.active) return false;

    m_spatialHash.remove(obj.spatialId);
    if (obj.nativePtr != nullptr) {
        m_ptrToIndex.erase(obj.nativePtr);
    }
    m_idToIndex.erase(it);
    obj.active = false;
    obj.isVisible = false;
    obj.nativePtr = nullptr;

    if (m_activeObjectCount > 0) {
        m_activeObjectCount--;
    }
    m_cameraCached = false;
    return true;
}

bool VisibilityCuller::unregisterObjectByPtr(void* nativePtr) {
    if (!nativePtr) return false;
    auto it = m_ptrToIndex.find(nativePtr);
    if (it == m_ptrToIndex.end()) return false;
    return unregisterObject(m_objects[it->second].id);
}

size_t VisibilityCuller::cullAgainstCamera(const core::Rect2D& cameraRect, std::vector<void*>& outVisibleObjects) {
    outVisibleObjects.clear();
    if (m_activeObjectCount == 0) {
        m_lastVisibleCount = 0;
        m_lastCulledCount = 0;
        return 0;
    }

    m_currentFrameStamp++;
    if (m_currentFrameStamp == 0) {
        for (auto& obj : m_objects) {
            obj.lastSeenStamp = 0;
        }
        m_currentFrameStamp = 1;
    }

    core::Rect2D queryRect = cameraRect.normalized().expanded(m_padX, m_padY);
    std::vector<uint32_t> visibleSpatialIds;
    visibleSpatialIds.reserve(256);
    m_spatialHash.queryIDs(queryRect, visibleSpatialIds);

    size_t newlyVis = 0;
    for (uint32_t spatialId : visibleSpatialIds) {
        if (spatialId >= m_objects.size()) continue;
        auto& obj = m_objects[spatialId];
        if (!obj.active) continue;
        obj.lastSeenStamp = m_currentFrameStamp;
        if (!obj.isVisible) {
            obj.isVisible = true;
            newlyVis++;
        }
        outVisibleObjects.push_back(obj.nativePtr);
    }

    size_t newlyCulled = 0;
    if (m_previouslyVisibleIndices.empty()) {
        for (size_t idx = 0; idx < m_objects.size(); ++idx) {
            auto& obj = m_objects[idx];
            if (obj.active && obj.lastSeenStamp != m_currentFrameStamp && obj.isVisible) {
                obj.isVisible = false;
                newlyCulled++;
            }
        }
    } else {
        for (uint32_t prevIdx : m_previouslyVisibleIndices) {
            if (prevIdx < m_objects.size()) {
                auto& obj = m_objects[prevIdx];
                if (obj.active && obj.lastSeenStamp != m_currentFrameStamp && obj.isVisible) {
                    obj.isVisible = false;
                    newlyCulled++;
                }
            }
        }
    }

    m_previouslyVisibleIndices = std::move(visibleSpatialIds);
    m_lastVisibleCount = outVisibleObjects.size();
    m_lastCulledCount = (m_activeObjectCount >= m_lastVisibleCount)
                            ? (m_activeObjectCount - m_lastVisibleCount)
                            : 0;
    m_lastNewlyVisibleCount = newlyVis;
    m_lastNewlyCulledCount = newlyCulled;
    m_lastCameraRect = cameraRect;
    m_cameraCached = true;

    return m_lastVisibleCount;
}

VisibilityDeltaResult VisibilityCuller::evaluateVisibilityDelta(
    const core::Rect2D& cameraRect,
    core::MemoryArena& arena,
    std::vector<void*>* outNewlyVisibleDecorative,
    std::vector<void*>* outNewlyCulledDecorative) {

    VisibilityDeltaResult res{};
    if (outNewlyVisibleDecorative) outNewlyVisibleDecorative->clear();
    if (outNewlyCulledDecorative) outNewlyCulledDecorative->clear();

    if (m_activeObjectCount == 0) {
        m_lastVisibleCount = 0;
        m_lastCulledCount = 0;
        m_lastNewlyVisibleCount = 0;
        m_lastNewlyCulledCount = 0;
        return res;
    }

    m_currentFrameStamp++;
    if (m_currentFrameStamp == 0) {
        for (auto& obj : m_objects) {
            obj.lastSeenStamp = 0;
        }
        m_currentFrameStamp = 1;
    }

    core::Rect2D queryRect = cameraRect.normalized().expanded(m_padX, m_padY);
    uint32_t* visibleSpatialIds = nullptr;
    size_t rawCount = m_spatialHash.queryIDsArena(queryRect, arena, visibleSpatialIds);

    size_t validVisible = 0;
    size_t newlyVis = 0;
    for (size_t i = 0; i < rawCount; ++i) {
        uint32_t idx = visibleSpatialIds[i];
        if (idx >= m_objects.size()) continue;
        auto& obj = m_objects[idx];
        if (!obj.active) continue;

        validVisible++;
        obj.lastSeenStamp = m_currentFrameStamp;
        if (!obj.isVisible) {
            obj.isVisible = true;
            newlyVis++;
            if (outNewlyVisibleDecorative && obj.isDecorative && obj.nativePtr) {
                outNewlyVisibleDecorative->push_back(obj.nativePtr);
            }
        }
    }

    size_t newlyCulled = 0;
    if (m_previouslyVisibleIndices.empty()) {
        // First evaluation after level load: mark all objects outside initial camera frustum as not visible
        for (size_t idx = 0; idx < m_objects.size(); ++idx) {
            auto& obj = m_objects[idx];
            if (obj.active && obj.lastSeenStamp != m_currentFrameStamp && obj.isVisible) {
                obj.isVisible = false;
                newlyCulled++;
                if (outNewlyCulledDecorative && obj.isDecorative && obj.nativePtr) {
                    outNewlyCulledDecorative->push_back(obj.nativePtr);
                }
            }
        }
    } else {
        for (uint32_t prevIdx : m_previouslyVisibleIndices) {
            if (prevIdx < m_objects.size()) {
                auto& obj = m_objects[prevIdx];
                if (obj.active && obj.lastSeenStamp != m_currentFrameStamp && obj.isVisible) {
                    obj.isVisible = false;
                    newlyCulled++;
                    if (outNewlyCulledDecorative && obj.isDecorative && obj.nativePtr) {
                        outNewlyCulledDecorative->push_back(obj.nativePtr);
                    }
                }
            }
        }
    }

    m_previouslyVisibleIndices.assign(visibleSpatialIds, visibleSpatialIds + rawCount);
    m_lastVisibleCount = validVisible;
    m_lastCulledCount = (m_activeObjectCount >= validVisible) ? (m_activeObjectCount - validVisible) : 0;
    m_lastNewlyVisibleCount = newlyVis;
    m_lastNewlyCulledCount = newlyCulled;
    m_lastCameraRect = cameraRect;
    m_cameraCached = true;

    res.visibleCount = m_lastVisibleCount;
    res.culledCount = m_lastCulledCount;
    res.newlyVisibleCount = newlyVis;
    res.newlyCulledCount = newlyCulled;
    return res;
}

const RenderObject* VisibilityCuller::getObjectById(uint32_t id) const {
    auto it = m_idToIndex.find(id);
    if (it == m_idToIndex.end()) return nullptr;
    const auto& obj = m_objects[it->second];
    return obj.active ? &obj : nullptr;
}

const RenderObject* VisibilityCuller::getObjectByPtr(void* nativePtr) const {
    if (!nativePtr) return nullptr;
    auto it = m_ptrToIndex.find(nativePtr);
    if (it == m_ptrToIndex.end()) return nullptr;
    const auto& obj = m_objects[it->second];
    return obj.active ? &obj : nullptr;
}

} // namespace optigd::optimization

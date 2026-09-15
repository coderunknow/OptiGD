#include "VisibilityCuller.hpp"
#include <cmath>

namespace optigd::optimization {

VisibilityCuller::VisibilityCuller(float cellSize)
    : m_spatialHash(cellSize) {}

void VisibilityCuller::setCellSize(float cellSize) {
    m_spatialHash.setCellSize(cellSize);
}

void VisibilityCuller::setPadding(float padX, float padY) {
    m_padX = padX;
    m_padY = padY;
}

void VisibilityCuller::clear() {
    m_spatialHash.clear();
    m_objects.clear();
    m_lastCulledCount = 0;
    m_lastVisibleCount = 0;
    m_cameraCached = false;
}

void VisibilityCuller::reserve(size_t capacity) {
    m_spatialHash.reserve(capacity);
    m_objects.reserve(capacity);
}

uint32_t VisibilityCuller::registerObject(uint32_t id, const core::Rect2D& bounds, void* nativePtr, bool isStatic) {
    m_objects.push_back({id, bounds, nativePtr, true, isStatic});
    m_spatialHash.insert(bounds, nativePtr);
    return id;
}

void VisibilityCuller::updateObjectBounds(uint32_t id, const core::Rect2D& newBounds) {
    if (id < m_objects.size()) {
        m_objects[id].bounds = newBounds;
        m_spatialHash.update(id, newBounds);
    }
}

size_t VisibilityCuller::cullAgainstCamera(const core::Rect2D& cameraRect, std::vector<void*>& outVisibleObjects) {
    outVisibleObjects.clear();
    if (m_objects.empty()) {
        m_lastVisibleCount = 0;
        m_lastCulledCount = 0;
        return 0;
    }

    core::Rect2D queryRect = cameraRect.expanded(m_padX, m_padY);
    m_spatialHash.query(queryRect, outVisibleObjects);

    m_lastVisibleCount = outVisibleObjects.size();
    m_lastCulledCount = (m_objects.size() >= m_lastVisibleCount) ? (m_objects.size() - m_lastVisibleCount) : 0;
    m_lastCameraRect = cameraRect;
    m_cameraCached = true;

    return m_lastVisibleCount;
}

} // namespace optigd::optimization

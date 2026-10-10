#include "VisibilityCuller.hpp"

#include <algorithm>
#include <cmath>

namespace optigd::optimization {

VisibilityCuller::VisibilityCuller(float cellSize)
    : m_spatialHash(cellSize) {}

void VisibilityCuller::setCellSize(float cellSize) {
    m_spatialHash.setCellSize(cellSize);
    m_lastCulledCount = 0;
    m_lastVisibleCount = 0;
}

void VisibilityCuller::setPadding(float padX, float padY) {
    m_padX = std::isfinite(padX) ? std::max(0.0f, padX) : 0.0f;
    m_padY = std::isfinite(padY) ? std::max(0.0f, padY) : 0.0f;
}

void VisibilityCuller::clear() {
    m_spatialHash.clear();
    m_lastCulledCount = 0;
    m_lastVisibleCount = 0;
}

void VisibilityCuller::reserve(size_t capacity) {
    m_spatialHash.reserve(capacity);
}

VisibilityCuller::Handle VisibilityCuller::registerObject(const core::Rect2D& bounds, void* nativePtr, bool isStatic) {
    (void)isStatic; // reserved for future dynamic-object policies; not used by the index
    return m_spatialHash.insert(bounds, nativePtr);
}

bool VisibilityCuller::updateObjectBounds(Handle handle, const core::Rect2D& newBounds) {
    return m_spatialHash.update(handle, newBounds);
}

bool VisibilityCuller::unregisterObject(Handle handle) {
    return m_spatialHash.remove(handle);
}

size_t VisibilityCuller::cullAgainstCamera(const core::Rect2D& cameraRect, std::vector<void*>& outVisibleObjects) {
    outVisibleObjects.clear();
    const size_t total = m_spatialHash.getItemCount();
    if (total == 0) {
        m_lastVisibleCount = 0;
        m_lastCulledCount = 0;
        return 0;
    }

    const core::Rect2D queryRect = cameraRect.expanded(m_padX, m_padY);
    m_spatialHash.query(queryRect, outVisibleObjects);

    m_lastVisibleCount = outVisibleObjects.size();
    m_lastCulledCount = total - m_lastVisibleCount;
    return m_lastVisibleCount;
}

} // namespace optigd::optimization

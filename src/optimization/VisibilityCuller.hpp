#pragma once

#include "../core/Types.hpp"
#include "../core/SpatialHash.hpp"
#include <vector>
#include <cstdint>

namespace optigd::optimization {

struct RenderObject {
    uint32_t id{0};
    core::Rect2D bounds{};
    void* nativePtr{nullptr};
    bool isVisible{true};
    bool isStatic{true};
};

class VisibilityCuller {
public:
    explicit VisibilityCuller(float cellSize = 128.0f);

    void setCellSize(float cellSize);
    void setPadding(float padX, float padY);

    void clear();
    void reserve(size_t capacity);

    uint32_t registerObject(uint32_t id, const core::Rect2D& bounds, void* nativePtr, bool isStatic = true);
    void updateObjectBounds(uint32_t id, const core::Rect2D& newBounds);

    // Evaluates camera frustum against registered objects
    // Returns number of visible objects
    size_t cullAgainstCamera(const core::Rect2D& cameraRect, std::vector<void*>& outVisibleObjects);

    size_t getTotalObjectCount() const { return m_objects.size(); }
    size_t getLastCulledCount() const { return m_lastCulledCount; }
    size_t getLastVisibleCount() const { return m_lastVisibleCount; }

private:
    float m_padX{150.0f};
    float m_padY{100.0f};

    core::SpatialHash m_spatialHash;
    std::vector<RenderObject> m_objects;

    core::Rect2D m_lastCameraRect{};
    bool m_cameraCached{false};
    size_t m_lastCulledCount{0};
    size_t m_lastVisibleCount{0};
};

} // namespace optigd::optimization

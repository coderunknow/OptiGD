#pragma once

#include "../core/SpatialHash.hpp"
#include "../core/Types.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace optigd::optimization {

// Spatial index over game objects with camera-rectangle queries.
//
// The culler only answers "which registered objects intersect the padded camera
// rectangle". It does not change any game state. Whether a caller acts on the
// answer is a separate decision; see docs/ARCHITECTURE.md (shadow mode).
class VisibilityCuller {
public:
    using Handle = uint32_t;
    static constexpr Handle kInvalidHandle = core::SpatialHash::kInvalidId;

    explicit VisibilityCuller(float cellSize = 128.0f);

    void setCellSize(float cellSize);   // clears all registrations
    void setPadding(float padX, float padY);
    float getPaddingX() const { return m_padX; }
    float getPaddingY() const { return m_padY; }

    // Removes all registrations.
    void clear();
    void reserve(size_t capacity);

    // Returns kInvalidHandle if bounds are not finite.
    Handle registerObject(const core::Rect2D& bounds, void* nativePtr, bool isStatic = true);
    bool updateObjectBounds(Handle handle, const core::Rect2D& newBounds);
    bool unregisterObject(Handle handle);

    // Fills outVisibleObjects with the native pointers of objects intersecting
    // cameraRect expanded by the padding. Returns the number of such objects.
    // Culled = live objects - visible.
    size_t cullAgainstCamera(const core::Rect2D& cameraRect, std::vector<void*>& outVisibleObjects);

    size_t getTotalObjectCount() const { return m_spatialHash.getItemCount(); }
    size_t getLastCulledCount() const { return m_lastCulledCount; }
    size_t getLastVisibleCount() const { return m_lastVisibleCount; }

private:
    float m_padX{120.0f};
    float m_padY{80.0f};

    core::SpatialHash m_spatialHash;
    size_t m_lastCulledCount{0};
    size_t m_lastVisibleCount{0};
};

} // namespace optigd::optimization

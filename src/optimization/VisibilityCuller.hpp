#pragma once

#include "../core/Types.hpp"
#include "../core/SpatialHash.hpp"
#include "../core/MemoryArena.hpp"
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cstddef>

namespace optigd::optimization {

struct RenderObject {
    uint32_t id{0};
    uint32_t spatialId{0};
    core::Rect2D bounds{};
    void* nativePtr{nullptr};
    bool isVisible{true};
    bool isStatic{true};
    bool isDecorative{false};
    bool active{true};
    uint32_t lastSeenStamp{0};
};

struct VisibilityDeltaResult {
    size_t visibleCount{0};
    size_t culledCount{0};
    size_t newlyVisibleCount{0};
    size_t newlyCulledCount{0};
};

class VisibilityCuller {
public:
    explicit VisibilityCuller(float cellSize = 128.0f);

    // Re-indexes every registered object with the new cell size instead of
    // silently dropping the spatial index. Changing the cell size no longer
    // causes every object to be reported as culled.
    void setCellSize(float cellSize);
    float getCellSize() const { return m_spatialHash.getCellSize(); }
    void setPadding(float padX, float padY);
    float getPaddingX() const { return m_padX; }
    float getPaddingY() const { return m_padY; }

    void clear();
    void reserve(size_t capacity);

    uint32_t registerObject(uint32_t id, const core::Rect2D& bounds, void* nativePtr,
                            bool isStatic = true, bool isDecorative = false);
    void updateObjectBounds(uint32_t id, const core::Rect2D& newBounds);
    bool updateObjectBoundsByPtr(void* nativePtr, const core::Rect2D& newBounds);
    bool unregisterObject(uint32_t id);
    bool unregisterObjectByPtr(void* nativePtr);

    // Evaluates camera frustum against registered objects
    // Returns number of visible objects
    size_t cullAgainstCamera(const core::Rect2D& cameraRect, std::vector<void*>& outVisibleObjects);

    // Arena-backed differential frustum evaluation (O(k) transitions instead of O(N) scan)
    VisibilityDeltaResult evaluateVisibilityDelta(
        const core::Rect2D& cameraRect,
        core::MemoryArena& arena,
        std::vector<void*>* outNewlyVisibleDecorative = nullptr,
        std::vector<void*>* outNewlyCulledDecorative = nullptr);

    // Restores the internal visibility state of every object OptiGD marked as
    // culled. Used when spatial culling is disabled so that decorative objects
    // hidden by an earlier frame are revealed again instead of staying hidden
    // for the rest of the level.
    // Returns the number of objects whose visibility flag changed back to true.
    size_t restoreAllVisibility(std::vector<void*>* outRestoredPointers = nullptr,
                                bool decorativeOnly = false);

    // True if at least one object is currently marked culled by OptiGD.
    bool hasCulledObjects() const { return m_culledByOptiGD > 0; }

    size_t getTotalObjectCount() const { return m_activeObjectCount; }
    size_t getLastCulledCount() const { return m_lastCulledCount; }
    size_t getLastVisibleCount() const { return m_lastVisibleCount; }
    size_t getLastNewlyVisibleCount() const { return m_lastNewlyVisibleCount; }
    size_t getLastNewlyCulledCount() const { return m_lastNewlyCulledCount; }

    const RenderObject* getObjectById(uint32_t id) const;
    const RenderObject* getObjectByPtr(void* nativePtr) const;

private:
    float m_padX{150.0f};
    float m_padY{100.0f};

    core::SpatialHash m_spatialHash;
    std::vector<RenderObject> m_objects;
    std::unordered_map<uint32_t, uint32_t> m_idToIndex;
    std::unordered_map<void*, uint32_t> m_ptrToIndex;
    // Differential tracking buffers. Kept as members so the steady-state
    // per-frame path performs zero heap allocations; swapped each frame.
    std::vector<uint32_t> m_previouslyVisibleIndices;
    std::vector<uint32_t> m_currentVisibleIndices;

    size_t m_activeObjectCount{0};
    size_t m_culledByOptiGD{0};
    uint32_t m_currentFrameStamp{1};

    core::Rect2D m_lastCameraRect{};
    bool m_cameraCached{false};
    size_t m_lastCulledCount{0};
    size_t m_lastVisibleCount{0};
    size_t m_lastNewlyVisibleCount{0};
    size_t m_lastNewlyCulledCount{0};

    // SpatialHash userData holds (objectIndex + 1) so index 0 never encodes as
    // a null pointer. This decouples object indices from SpatialHash item ids,
    // which is required because re-indexing after a cell-size change renumbers
    // the SpatialHash items.
    void* encodeIndex(uint32_t objIndex) const {
        return reinterpret_cast<void*>(static_cast<uintptr_t>(objIndex) + 1u);
    }
    uint32_t decodeIndex(void* userData) const {
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(userData) - 1u);
    }

    void rebuildSpatialIndex();
};

} // namespace optigd::optimization

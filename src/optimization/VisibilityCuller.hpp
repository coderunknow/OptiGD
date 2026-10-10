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

    void setCellSize(float cellSize);
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
        std::vector<void*>* outNewlyCulledDecorative = nullptr
    );

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
    std::vector<uint32_t> m_previouslyVisibleIndices;

    size_t m_activeObjectCount{0};
    uint32_t m_currentFrameStamp{1};

    core::Rect2D m_lastCameraRect{};
    bool m_cameraCached{false};
    size_t m_lastCulledCount{0};
    size_t m_lastVisibleCount{0};
    size_t m_lastNewlyVisibleCount{0};
    size_t m_lastNewlyCulledCount{0};
};

} // namespace optigd::optimization

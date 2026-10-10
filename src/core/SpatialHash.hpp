#pragma once

#include "Types.hpp"
#include "MemoryArena.hpp"
#include <vector>
#include <cstdint>
#include <cstddef>

namespace optigd::core {

struct SpatialItem {
    uint32_t id{0};
    Rect2D bounds{};
    void* userData{nullptr};
    uint32_t lastQueryStamp{0};
    int32_t minCellX{0};
    int32_t maxCellX{-1};
    int32_t minCellY{0};
    int32_t maxCellY{-1};
    bool active{true};
};

class SpatialHash {
public:
    explicit SpatialHash(float cellSize = 128.0f, size_t initialBuckets = 4096);

    void setCellSize(float cellSize);
    float getCellSize() const { return m_cellSize; }

    void clear();
    void reserve(size_t capacity);

    uint32_t insert(const Rect2D& bounds, void* userData);
    void update(uint32_t id, const Rect2D& newBounds);
    bool remove(uint32_t id);

    // Queries items intersecting queryRect and writes their pointers/IDs to outResults
    size_t query(const Rect2D& queryRect, std::vector<void*>& outResults);
    size_t queryIDs(const Rect2D& queryRect, std::vector<uint32_t>& outResults);

    // Arena-backed zero-heap-allocation query returning slice allocated from arena
    size_t queryArena(const Rect2D& queryRect, MemoryArena& arena, void**& outPtrs);
    size_t queryIDsArena(const Rect2D& queryRect, MemoryArena& arena, uint32_t*& outIds);

    size_t getItemCount() const { return m_items.size(); }
    size_t getActiveItemCount() const { return m_activeItemCount; }
    size_t getActiveObjectCount() const { return m_activeItemCount; }
    size_t getBucketCount() const { return m_buckets.size(); }

    const SpatialItem& getItem(uint32_t id) const { return m_items[id]; }

private:
    static constexpr int32_t kMaxCellSpan = 64;

    float m_cellSize;
    float m_invCellSize;
    uint32_t m_currentQueryStamp{1};
    size_t m_activeItemCount{0};

    std::vector<SpatialItem> m_items;

    static uint64_t hashCoords(int32_t cx, int32_t cy) {
        uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(cx));
        uint64_t uy = static_cast<uint64_t>(static_cast<uint32_t>(cy));
        return (ux * 73856093ULL) ^ (uy * 19349663ULL);
    }

    std::vector<std::vector<uint32_t>> m_buckets;
    size_t m_bucketMask;

    void computeCellRange(const Rect2D& normBounds,
                          int32_t& outMinX, int32_t& outMaxX,
                          int32_t& outMinY, int32_t& outMaxY) const;
    void insertToCell(int32_t cx, int32_t cy, uint32_t itemId);
    void removeFromCell(int32_t cx, int32_t cy, uint32_t itemId);
    void advanceQueryStamp();
};

} // namespace optigd::core

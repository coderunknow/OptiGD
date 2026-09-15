#pragma once

#include "Types.hpp"
#include <vector>
#include <cstdint>
#include <cstddef>

namespace optigd::core {

struct SpatialItem {
    uint32_t id{0};
    Rect2D bounds{};
    void* userData{nullptr};
    uint32_t lastQueryStamp{0};
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

    // Queries items intersecting queryRect and appends their IDs/pointers to outResults
    void query(const Rect2D& queryRect, std::vector<void*>& outResults);
    void queryIDs(const Rect2D& queryRect, std::vector<uint32_t>& outResults);

    size_t getItemCount() const { return m_items.size(); }
    size_t getBucketCount() const { return m_buckets.size(); }

    const SpatialItem& getItem(uint32_t id) const { return m_items[id]; }

private:
    float m_cellSize;
    float m_invCellSize;
    uint32_t m_currentQueryStamp{1};

    std::vector<SpatialItem> m_items;

    // Fast 64-bit coordinate hash to bucket
    struct CellCoord {
        int32_t x;
        int32_t y;
    };

    static uint64_t hashCoords(int32_t cx, int32_t cy) {
        // Fast 64-bit mixing
        uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(cx));
        uint64_t uy = static_cast<uint64_t>(static_cast<uint32_t>(cy));
        return (ux * 73856093ULL) ^ (uy * 19349663ULL);
    }

    struct BucketEntry {
        uint64_t cellKey{0};
        uint32_t itemId{0};
    };

    std::vector<std::vector<uint32_t>> m_buckets;
    size_t m_bucketMask;

    void insertToCell(int32_t cx, int32_t cy, uint32_t itemId);
};

} // namespace optigd::core

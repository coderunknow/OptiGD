#pragma once

#include "Types.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace optigd::core {

struct SpatialItem {
    uint32_t id{0};
    Rect2D bounds{};            // normalized: size is non-negative
    void* userData{nullptr};
    uint32_t lastQueryStamp{0};
    bool alive{false};
    bool large{false};          // true when stored on the overflow list instead of cell buckets
    int32_t cellX0{0}, cellY0{0}, cellX1{-1}, cellY1{-1}; // cell span used when placed
};

// 2D uniform-grid spatial index over axis-aligned rectangles.
//
// Semantics (all covered by tests):
//  - insert/update/remove keep the cell buckets consistent with the stored bounds.
//  - Queries use inclusive intersection, return every live item whose bounds
//    intersect the query rectangle exactly once, and never return removed items.
//  - Negative coordinates are supported. Cell coordinates are clamped to a large
//    finite range; clamping is monotone, so it can only cost efficiency, never
//    correctness.
//  - Items with non-finite bounds are rejected (insert returns kInvalidId,
//    update/remove return false).
//  - Items covering more than kMaxCellsPerItem cells are kept on an overflow list
//    that every query tests directly, which bounds memory use.
//  - Ids are dense, stable for the life of the index, and are not reused until clear().
//    Removal leaves a tombstone; clear() resets everything.
class SpatialHash {
public:
    static constexpr uint32_t kInvalidId = 0xFFFFFFFFu;
    static constexpr size_t kMaxCellsPerItem = 1024;
    static constexpr int64_t kMaxCellCoord = int64_t{1} << 24;
    static constexpr size_t kMaxCellsPerQuery = 65536;

    explicit SpatialHash(float cellSize = 128.0f, size_t initialBuckets = 4096);

    // Changing the cell size invalidates all items (clears the index).
    void setCellSize(float cellSize);
    float getCellSize() const { return m_cellSize; }

    void clear();
    void reserve(size_t capacity);

    uint32_t insert(const Rect2D& bounds, void* userData);
    bool update(uint32_t id, const Rect2D& newBounds);
    bool remove(uint32_t id);
    bool isAlive(uint32_t id) const;

    // Appends the userData of every matching live item (caller clears the vector if needed).
    void query(const Rect2D& queryRect, std::vector<void*>& outResults);
    // Appends the ids of every matching live item.
    void queryIDs(const Rect2D& queryRect, std::vector<uint32_t>& outResults);

    size_t getItemCount() const { return m_aliveCount; }
    size_t getBucketCount() const { return m_buckets.size(); }
    size_t getLargeItemCount() const { return m_largeItems.size(); }

    // id must be a valid id returned by insert().
    const SpatialItem& getItem(uint32_t id) const { return m_items[id]; }

private:
    struct CellEntry {
        int32_t cx;
        int32_t cy;
        uint32_t itemId;
    };

    struct Span {
        int32_t x0{0}, y0{0}, x1{-1}, y1{-1};
    };

    // Returns false if the rectangle has non-finite coordinates.
    static bool normalize(const Rect2D& in, Rect2D& out);
    int32_t cellOf(float value) const;
    Span spanOf(const Rect2D& normalized) const;
    size_t bucketIndex(int32_t cx, int32_t cy) const;
    void placeItem(uint32_t id);   // adds the item's span (or overflow list) to the index
    void unplaceItem(uint32_t id); // removes the item's entries from the index
    void nextStamp();

    float m_cellSize;
    float m_invCellSize;
    uint32_t m_currentQueryStamp{1};
    size_t m_aliveCount{0};

    std::vector<SpatialItem> m_items;
    std::vector<uint32_t> m_largeItems;
    std::vector<std::vector<CellEntry>> m_buckets;
    size_t m_bucketMask{0};
};

} // namespace optigd::core

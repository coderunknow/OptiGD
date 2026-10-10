#include "SpatialHash.hpp"
#include <cmath>
#include <algorithm>

namespace optigd::core {

SpatialHash::SpatialHash(float cellSize, size_t initialBuckets)
    : m_cellSize(cellSize > 0.0f && std::isfinite(cellSize) ? cellSize : 128.0f),
      m_invCellSize(1.0f / m_cellSize) {
    size_t buckets = 256;
    while (buckets < initialBuckets && buckets < (static_cast<size_t>(1) << 24)) {
        buckets <<= 1;
    }
    m_buckets.resize(buckets);
    m_bucketMask = buckets - 1;
}

void SpatialHash::setCellSize(float cellSize) {
    if (cellSize <= 0.0f || !std::isfinite(cellSize)) {
        cellSize = 128.0f;
    }
    m_cellSize = cellSize;
    m_invCellSize = 1.0f / cellSize;
    clear();
}

void SpatialHash::clear() {
    m_items.clear();
    for (auto& bucket : m_buckets) {
        bucket.clear();
    }
    m_currentQueryStamp = 1;
    m_activeItemCount = 0;
}

void SpatialHash::reserve(size_t capacity) {
    m_items.reserve(capacity);
}

void SpatialHash::computeCellRange(const Rect2D& normBounds,
                                   int32_t& outMinX, int32_t& outMaxX,
                                   int32_t& outMinY, int32_t& outMaxY) const {
    float minXf = std::clamp(normBounds.minX() * m_invCellSize, -1e7f, 1e7f);
    float maxXf = std::clamp(normBounds.maxX() * m_invCellSize, -1e7f, 1e7f);
    float minYf = std::clamp(normBounds.minY() * m_invCellSize, -1e7f, 1e7f);
    float maxYf = std::clamp(normBounds.maxY() * m_invCellSize, -1e7f, 1e7f);

    outMinX = static_cast<int32_t>(std::floor(minXf));
    outMaxX = static_cast<int32_t>(std::floor(maxXf));
    outMinY = static_cast<int32_t>(std::floor(minYf));
    outMaxY = static_cast<int32_t>(std::floor(maxYf));

    if (outMaxX - outMinX > kMaxCellSpan) {
        outMaxX = outMinX + kMaxCellSpan;
    }
    if (outMaxY - outMinY > kMaxCellSpan) {
        outMaxY = outMinY + kMaxCellSpan;
    }
}

void SpatialHash::insertToCell(int32_t cx, int32_t cy, uint32_t itemId) {
    uint64_t hash = hashCoords(cx, cy);
    size_t idx = hash & m_bucketMask;
    m_buckets[idx].push_back(itemId);
}

void SpatialHash::removeFromCell(int32_t cx, int32_t cy, uint32_t itemId) {
    uint64_t hash = hashCoords(cx, cy);
    size_t idx = hash & m_bucketMask;
    auto& bucket = m_buckets[idx];
    for (size_t i = 0; i < bucket.size(); ++i) {
        if (bucket[i] == itemId) {
            bucket[i] = bucket.back();
            bucket.pop_back();
            return;
        }
    }
}

uint32_t SpatialHash::insert(const Rect2D& bounds, void* userData) {
    Rect2D norm = bounds.normalized();
    int32_t minX = 0, maxX = 0, minY = 0, maxY = 0;
    computeCellRange(norm, minX, maxX, minY, maxY);

    uint32_t id = static_cast<uint32_t>(m_items.size());
    m_items.push_back({id, norm, userData, 0, minX, maxX, minY, maxY, true});
    m_activeItemCount++;

    for (int32_t cy = minY; cy <= maxY; ++cy) {
        for (int32_t cx = minX; cx <= maxX; ++cx) {
            insertToCell(cx, cy, id);
        }
    }

    return id;
}

void SpatialHash::update(uint32_t id, const Rect2D& newBounds) {
    if (id >= m_items.size()) return;
    auto& item = m_items[id];
    if (!item.active) return;

    Rect2D norm = newBounds.normalized();
    int32_t newMinX = 0, newMaxX = 0, newMinY = 0, newMaxY = 0;
    computeCellRange(norm, newMinX, newMaxX, newMinY, newMaxY);

    item.bounds = norm;

    // Fast path: if cell footprint is unchanged, no bucket updates needed
    if (newMinX == item.minCellX && newMaxX == item.maxCellX &&
        newMinY == item.minCellY && newMaxY == item.maxCellY) {
        return;
    }

    // Remove from old cells not covered by new cell range
    for (int32_t cy = item.minCellY; cy <= item.maxCellY; ++cy) {
        for (int32_t cx = item.minCellX; cx <= item.maxCellX; ++cx) {
            if (cx < newMinX || cx > newMaxX || cy < newMinY || cy > newMaxY) {
                removeFromCell(cx, cy, id);
            }
        }
    }

    // Insert into new cells not covered by old cell range
    for (int32_t cy = newMinY; cy <= newMaxY; ++cy) {
        for (int32_t cx = newMinX; cx <= newMaxX; ++cx) {
            if (cx < item.minCellX || cx > item.maxCellX || cy < item.minCellY || cy > item.maxCellY) {
                insertToCell(cx, cy, id);
            }
        }
    }

    item.minCellX = newMinX;
    item.maxCellX = newMaxX;
    item.minCellY = newMinY;
    item.maxCellY = newMaxY;
}

bool SpatialHash::remove(uint32_t id) {
    if (id >= m_items.size()) return false;
    auto& item = m_items[id];
    if (!item.active) return false;

    for (int32_t cy = item.minCellY; cy <= item.maxCellY; ++cy) {
        for (int32_t cx = item.minCellX; cx <= item.maxCellX; ++cx) {
            removeFromCell(cx, cy, id);
        }
    }

    item.active = false;
    item.userData = nullptr;
    item.minCellX = 0;
    item.maxCellX = -1;
    item.minCellY = 0;
    item.maxCellY = -1;
    if (m_activeItemCount > 0) {
        m_activeItemCount--;
    }
    return true;
}

void SpatialHash::advanceQueryStamp() {
    m_currentQueryStamp++;
    if (m_currentQueryStamp == 0) {
        for (auto& item : m_items) {
            item.lastQueryStamp = 0;
        }
        m_currentQueryStamp = 1;
    }
}

size_t SpatialHash::query(const Rect2D& queryRect, std::vector<void*>& outResults) {
    outResults.clear();
    if (m_activeItemCount == 0) return 0;

    Rect2D normQuery = queryRect.normalized();
    advanceQueryStamp();

    int32_t minX = 0, maxX = 0, minY = 0, maxY = 0;
    computeCellRange(normQuery, minX, maxX, minY, maxY);

    for (int32_t cy = minY; cy <= maxY; ++cy) {
        for (int32_t cx = minX; cx <= maxX; ++cx) {
            uint64_t hash = hashCoords(cx, cy);
            size_t idx = hash & m_bucketMask;
            const auto& bucket = m_buckets[idx];
            for (uint32_t itemId : bucket) {
                auto& item = m_items[itemId];
                if (!item.active || item.lastQueryStamp == m_currentQueryStamp) {
                    continue;
                }
                item.lastQueryStamp = m_currentQueryStamp;
                if (item.bounds.intersects(normQuery)) {
                    outResults.push_back(item.userData);
                }
            }
        }
    }
    return outResults.size();
}

size_t SpatialHash::queryIDs(const Rect2D& queryRect, std::vector<uint32_t>& outResults) {
    outResults.clear();
    if (m_activeItemCount == 0) return 0;

    Rect2D normQuery = queryRect.normalized();
    advanceQueryStamp();

    int32_t minX = 0, maxX = 0, minY = 0, maxY = 0;
    computeCellRange(normQuery, minX, maxX, minY, maxY);

    for (int32_t cy = minY; cy <= maxY; ++cy) {
        for (int32_t cx = minX; cx <= maxX; ++cx) {
            uint64_t hash = hashCoords(cx, cy);
            size_t idx = hash & m_bucketMask;
            const auto& bucket = m_buckets[idx];
            for (uint32_t itemId : bucket) {
                auto& item = m_items[itemId];
                if (!item.active || item.lastQueryStamp == m_currentQueryStamp) {
                    continue;
                }
                item.lastQueryStamp = m_currentQueryStamp;
                if (item.bounds.intersects(normQuery)) {
                    outResults.push_back(itemId);
                }
            }
        }
    }
    return outResults.size();
}

size_t SpatialHash::queryArena(const Rect2D& queryRect, MemoryArena& arena, void**& outPtrs) {
    outPtrs = nullptr;
    if (m_activeItemCount == 0) return 0;

    size_t maxResults = m_activeItemCount;
    void** buffer = arena.allocateArray<void*>(maxResults);
    if (!buffer) return 0;

    Rect2D normQuery = queryRect.normalized();
    advanceQueryStamp();

    int32_t minX = 0, maxX = 0, minY = 0, maxY = 0;
    computeCellRange(normQuery, minX, maxX, minY, maxY);

    size_t count = 0;
    for (int32_t cy = minY; cy <= maxY; ++cy) {
        for (int32_t cx = minX; cx <= maxX; ++cx) {
            uint64_t hash = hashCoords(cx, cy);
            size_t idx = hash & m_bucketMask;
            const auto& bucket = m_buckets[idx];
            for (uint32_t itemId : bucket) {
                auto& item = m_items[itemId];
                if (!item.active || item.lastQueryStamp == m_currentQueryStamp) {
                    continue;
                }
                item.lastQueryStamp = m_currentQueryStamp;
                if (item.bounds.intersects(normQuery) && count < maxResults) {
                    buffer[count++] = item.userData;
                }
            }
        }
    }

    outPtrs = buffer;
    return count;
}

size_t SpatialHash::queryIDsArena(const Rect2D& queryRect, MemoryArena& arena, uint32_t*& outIds) {
    outIds = nullptr;
    if (m_activeItemCount == 0) return 0;

    size_t maxResults = m_activeItemCount;
    uint32_t* buffer = arena.allocateArray<uint32_t>(maxResults);
    if (!buffer) return 0;

    Rect2D normQuery = queryRect.normalized();
    advanceQueryStamp();

    int32_t minX = 0, maxX = 0, minY = 0, maxY = 0;
    computeCellRange(normQuery, minX, maxX, minY, maxY);

    size_t count = 0;
    for (int32_t cy = minY; cy <= maxY; ++cy) {
        for (int32_t cx = minX; cx <= maxX; ++cx) {
            uint64_t hash = hashCoords(cx, cy);
            size_t idx = hash & m_bucketMask;
            const auto& bucket = m_buckets[idx];
            for (uint32_t itemId : bucket) {
                auto& item = m_items[itemId];
                if (!item.active || item.lastQueryStamp == m_currentQueryStamp) {
                    continue;
                }
                item.lastQueryStamp = m_currentQueryStamp;
                if (item.bounds.intersects(normQuery) && count < maxResults) {
                    buffer[count++] = itemId;
                }
            }
        }
    }

    outIds = buffer;
    return count;
}

} // namespace optigd::core

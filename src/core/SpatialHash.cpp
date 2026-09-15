#include "SpatialHash.hpp"
#include <cmath>
#include <algorithm>

namespace optigd::core {

SpatialHash::SpatialHash(float cellSize, size_t initialBuckets)
    : m_cellSize(cellSize), m_invCellSize(1.0f / cellSize) {
    // Round initialBuckets up to nearest power of 2
    size_t buckets = 256;
    while (buckets < initialBuckets) {
        buckets <<= 1;
    }
    m_buckets.resize(buckets);
    m_bucketMask = buckets - 1;
}

void SpatialHash::setCellSize(float cellSize) {
    if (cellSize <= 0.0f) cellSize = 128.0f;
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
}

void SpatialHash::reserve(size_t capacity) {
    m_items.reserve(capacity);
}

void SpatialHash::insertToCell(int32_t cx, int32_t cy, uint32_t itemId) {
    uint64_t hash = hashCoords(cx, cy);
    size_t idx = hash & m_bucketMask;
    m_buckets[idx].push_back(itemId);
}

uint32_t SpatialHash::insert(const Rect2D& bounds, void* userData) {
    uint32_t id = static_cast<uint32_t>(m_items.size());
    m_items.push_back({id, bounds, userData, 0});

    int32_t minX = static_cast<int32_t>(std::floor(bounds.minX() * m_invCellSize));
    int32_t maxX = static_cast<int32_t>(std::floor(bounds.maxX() * m_invCellSize));
    int32_t minY = static_cast<int32_t>(std::floor(bounds.minY() * m_invCellSize));
    int32_t maxY = static_cast<int32_t>(std::floor(bounds.maxY() * m_invCellSize));

    for (int32_t cy = minY; cy <= maxY; ++cy) {
        for (int32_t cx = minX; cx <= maxX; ++cx) {
            insertToCell(cx, cy, id);
        }
    }

    return id;
}

void SpatialHash::update(uint32_t id, const Rect2D& newBounds) {
    if (id >= m_items.size()) return;
    m_items[id].bounds = newBounds;
    // Note: for dynamic objects, caller can clear and reinsert or rebuild grid per frame
}

void SpatialHash::query(const Rect2D& queryRect, std::vector<void*>& outResults) {
    if (m_items.empty()) return;

    m_currentQueryStamp++;
    if (m_currentQueryStamp == 0) {
        // Overflow wrap-around
        for (auto& item : m_items) {
            item.lastQueryStamp = 0;
        }
        m_currentQueryStamp = 1;
    }

    int32_t minX = static_cast<int32_t>(std::floor(queryRect.minX() * m_invCellSize));
    int32_t maxX = static_cast<int32_t>(std::floor(queryRect.maxX() * m_invCellSize));
    int32_t minY = static_cast<int32_t>(std::floor(queryRect.minY() * m_invCellSize));
    int32_t maxY = static_cast<int32_t>(std::floor(queryRect.maxY() * m_invCellSize));

    for (int32_t cy = minY; cy <= maxY; ++cy) {
        for (int32_t cx = minX; cx <= maxX; ++cx) {
            uint64_t hash = hashCoords(cx, cy);
            size_t idx = hash & m_bucketMask;
            const auto& bucket = m_buckets[idx];
            for (uint32_t itemId : bucket) {
                auto& item = m_items[itemId];
                if (item.lastQueryStamp == m_currentQueryStamp) {
                    continue; // Deduplicated
                }
                item.lastQueryStamp = m_currentQueryStamp;
                if (item.bounds.intersects(queryRect)) {
                    outResults.push_back(item.userData);
                }
            }
        }
    }
}

void SpatialHash::queryIDs(const Rect2D& queryRect, std::vector<uint32_t>& outResults) {
    if (m_items.empty()) return;

    m_currentQueryStamp++;
    if (m_currentQueryStamp == 0) {
        for (auto& item : m_items) {
            item.lastQueryStamp = 0;
        }
        m_currentQueryStamp = 1;
    }

    int32_t minX = static_cast<int32_t>(std::floor(queryRect.minX() * m_invCellSize));
    int32_t maxX = static_cast<int32_t>(std::floor(queryRect.maxX() * m_invCellSize));
    int32_t minY = static_cast<int32_t>(std::floor(queryRect.minY() * m_invCellSize));
    int32_t maxY = static_cast<int32_t>(std::floor(queryRect.maxY() * m_invCellSize));

    for (int32_t cy = minY; cy <= maxY; ++cy) {
        for (int32_t cx = minX; cx <= maxX; ++cx) {
            uint64_t hash = hashCoords(cx, cy);
            size_t idx = hash & m_bucketMask;
            const auto& bucket = m_buckets[idx];
            for (uint32_t itemId : bucket) {
                auto& item = m_items[itemId];
                if (item.lastQueryStamp == m_currentQueryStamp) {
                    continue;
                }
                item.lastQueryStamp = m_currentQueryStamp;
                if (item.bounds.intersects(queryRect)) {
                    outResults.push_back(itemId);
                }
            }
        }
    }
}

} // namespace optigd::core

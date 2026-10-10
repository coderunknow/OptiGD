#include "SpatialHash.hpp"

#include <algorithm>
#include <cmath>

namespace optigd::core {

namespace {

constexpr float kDefaultCellSize = 128.0f;

size_t roundUpPow2(size_t value) {
    size_t result = 256;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

} // namespace

SpatialHash::SpatialHash(float cellSize, size_t initialBuckets) {
    if (!(cellSize > 0.0f) || !std::isfinite(cellSize)) {
        cellSize = kDefaultCellSize;
    }
    m_cellSize = cellSize;
    m_invCellSize = 1.0f / cellSize;
    const size_t buckets = roundUpPow2(initialBuckets);
    m_buckets.resize(buckets);
    m_bucketMask = buckets - 1;
}

void SpatialHash::setCellSize(float cellSize) {
    if (!(cellSize > 0.0f) || !std::isfinite(cellSize)) {
        cellSize = kDefaultCellSize;
    }
    m_cellSize = cellSize;
    m_invCellSize = 1.0f / cellSize;
    clear();
}

void SpatialHash::clear() {
    m_items.clear();
    m_largeItems.clear();
    for (auto& bucket : m_buckets) {
        bucket.clear();
    }
    m_aliveCount = 0;
    m_currentQueryStamp = 1;
}

void SpatialHash::reserve(size_t capacity) {
    m_items.reserve(capacity);
}

bool SpatialHash::normalize(const Rect2D& in, Rect2D& out) {
    const float x0 = in.origin.x;
    const float y0 = in.origin.y;
    const float x1 = in.origin.x + in.size.width;
    const float y1 = in.origin.y + in.size.height;
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1)) {
        return false;
    }
    const float lx = std::min(x0, x1);
    const float ly = std::min(y0, y1);
    const float hx = std::max(x0, x1);
    const float hy = std::max(y0, y1);
    out = Rect2D(lx, ly, hx - lx, hy - ly);
    return true;
}

int32_t SpatialHash::cellOf(float value) const {
    const double scaled = std::floor(static_cast<double>(value) * static_cast<double>(m_invCellSize));
    const double clamped = std::clamp(scaled, -static_cast<double>(kMaxCellCoord),
                                      static_cast<double>(kMaxCellCoord));
    return static_cast<int32_t>(clamped);
}

SpatialHash::Span SpatialHash::spanOf(const Rect2D& normalized) const {
    Span span;
    span.x0 = cellOf(normalized.minX());
    span.x1 = cellOf(normalized.maxX());
    span.y0 = cellOf(normalized.minY());
    span.y1 = cellOf(normalized.maxY());
    return span;
}

size_t SpatialHash::bucketIndex(int32_t cx, int32_t cy) const {
    // splitmix64-style mixing so neighbouring cells do not share low bits.
    uint64_t h = static_cast<uint64_t>(static_cast<uint32_t>(cx)) * 0x9E3779B97F4A7C15ull;
    h ^= (static_cast<uint64_t>(static_cast<uint32_t>(cy)) + 0x632BE59BD9B4E019ull) * 0xC2B2AE3D27D4EB4Full;
    h ^= h >> 29;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 32;
    return static_cast<size_t>(h) & m_bucketMask;
}

void SpatialHash::placeItem(uint32_t id) {
    SpatialItem& item = m_items[id];
    const Span span = spanOf(item.bounds);
    item.cellX0 = span.x0;
    item.cellY0 = span.y0;
    item.cellX1 = span.x1;
    item.cellY1 = span.y1;

    const int64_t cols = static_cast<int64_t>(span.x1) - span.x0 + 1;
    const int64_t rows = static_cast<int64_t>(span.y1) - span.y0 + 1;
    if (cols * rows > static_cast<int64_t>(kMaxCellsPerItem)) {
        item.large = true;
        m_largeItems.push_back(id);
        return;
    }

    item.large = false;
    for (int32_t cy = span.y0; cy <= span.y1; ++cy) {
        for (int32_t cx = span.x0; cx <= span.x1; ++cx) {
            m_buckets[bucketIndex(cx, cy)].push_back({cx, cy, id});
        }
    }
}

void SpatialHash::unplaceItem(uint32_t id) {
    SpatialItem& item = m_items[id];
    if (item.large) {
        auto it = std::find(m_largeItems.begin(), m_largeItems.end(), id);
        if (it != m_largeItems.end()) {
            *it = m_largeItems.back();
            m_largeItems.pop_back();
        }
        item.large = false;
        return;
    }

    for (int32_t cy = item.cellY0; cy <= item.cellY1; ++cy) {
        for (int32_t cx = item.cellX0; cx <= item.cellX1; ++cx) {
            auto& bucket = m_buckets[bucketIndex(cx, cy)];
            for (size_t i = 0; i < bucket.size(); ++i) {
                if (bucket[i].itemId == id && bucket[i].cx == cx && bucket[i].cy == cy) {
                    bucket[i] = bucket.back();
                    bucket.pop_back();
                    break; // one entry per (item, cell)
                }
            }
        }
    }
}

uint32_t SpatialHash::insert(const Rect2D& bounds, void* userData) {
    Rect2D normalized;
    if (!normalize(bounds, normalized)) {
        return kInvalidId;
    }
    const uint32_t id = static_cast<uint32_t>(m_items.size());
    SpatialItem item;
    item.id = id;
    item.bounds = normalized;
    item.userData = userData;
    item.lastQueryStamp = 0;
    item.alive = true;
    m_items.push_back(item);
    placeItem(id);
    m_aliveCount++;
    return id;
}

bool SpatialHash::update(uint32_t id, const Rect2D& newBounds) {
    if (id >= m_items.size() || !m_items[id].alive) {
        return false;
    }
    Rect2D normalized;
    if (!normalize(newBounds, normalized)) {
        return false;
    }
    unplaceItem(id);
    m_items[id].bounds = normalized;
    placeItem(id);
    return true;
}

bool SpatialHash::remove(uint32_t id) {
    if (id >= m_items.size() || !m_items[id].alive) {
        return false;
    }
    unplaceItem(id);
    m_items[id].alive = false;
    m_aliveCount--;
    return true;
}

bool SpatialHash::isAlive(uint32_t id) const {
    return id < m_items.size() && m_items[id].alive;
}

void SpatialHash::nextStamp() {
    m_currentQueryStamp++;
    if (m_currentQueryStamp == 0) {
        // Stamp counter wrapped: clear all item stamps so no stale match survives.
        for (auto& item : m_items) {
            item.lastQueryStamp = 0;
        }
        m_currentQueryStamp = 1;
    }
}

void SpatialHash::query(const Rect2D& queryRect, std::vector<void*>& outResults) {
    if (m_aliveCount == 0) return;
    Rect2D normalized;
    if (!normalize(queryRect, normalized)) return;
    nextStamp();

    const Span span = spanOf(normalized);
    const int64_t cells = (static_cast<int64_t>(span.x1) - span.x0 + 1) *
                          (static_cast<int64_t>(span.y1) - span.y0 + 1);
    if (cells > static_cast<int64_t>(kMaxCellsPerQuery)) {
        // Query covers more cells than it is worth indexing: exact linear scan (bounded by item count).
        for (auto& item : m_items) {
            if (item.alive && item.bounds.intersects(normalized)) {
                outResults.push_back(item.userData);
            }
        }
        return;
    }

    for (int32_t cy = span.y0; cy <= span.y1; ++cy) {
        for (int32_t cx = span.x0; cx <= span.x1; ++cx) {
            for (const CellEntry& entry : m_buckets[bucketIndex(cx, cy)]) {
                if (entry.cx != cx || entry.cy != cy) continue;
                SpatialItem& item = m_items[entry.itemId];
                if (item.lastQueryStamp == m_currentQueryStamp) continue;
                item.lastQueryStamp = m_currentQueryStamp;
                if (item.bounds.intersects(normalized)) {
                    outResults.push_back(item.userData);
                }
            }
        }
    }
    for (uint32_t id : m_largeItems) {
        const SpatialItem& item = m_items[id];
        if (item.bounds.intersects(normalized)) {
            outResults.push_back(item.userData);
        }
    }
}

void SpatialHash::queryIDs(const Rect2D& queryRect, std::vector<uint32_t>& outResults) {
    if (m_aliveCount == 0) return;
    Rect2D normalized;
    if (!normalize(queryRect, normalized)) return;
    nextStamp();

    const Span span = spanOf(normalized);
    const int64_t cells = (static_cast<int64_t>(span.x1) - span.x0 + 1) *
                          (static_cast<int64_t>(span.y1) - span.y0 + 1);
    if (cells > static_cast<int64_t>(kMaxCellsPerQuery)) {
        for (auto& item : m_items) {
            if (item.alive && item.bounds.intersects(normalized)) {
                outResults.push_back(item.id);
            }
        }
        return;
    }

    for (int32_t cy = span.y0; cy <= span.y1; ++cy) {
        for (int32_t cx = span.x0; cx <= span.x1; ++cx) {
            for (const CellEntry& entry : m_buckets[bucketIndex(cx, cy)]) {
                if (entry.cx != cx || entry.cy != cy) continue;
                SpatialItem& item = m_items[entry.itemId];
                if (item.lastQueryStamp == m_currentQueryStamp) continue;
                item.lastQueryStamp = m_currentQueryStamp;
                if (item.bounds.intersects(normalized)) {
                    outResults.push_back(item.id);
                }
            }
        }
    }
    for (uint32_t id : m_largeItems) {
        const SpatialItem& item = m_items[id];
        if (item.bounds.intersects(normalized)) {
            outResults.push_back(item.id);
        }
    }
}

} // namespace optigd::core

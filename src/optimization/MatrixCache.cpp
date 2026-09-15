#include "MatrixCache.hpp"

namespace optigd::optimization {

MatrixCache& MatrixCache::instance() {
    static MatrixCache s_instance;
    return s_instance;
}

void MatrixCache::clear() {
    m_cache.clear();
    m_hits = 0;
    m_misses = 0;
}

void MatrixCache::markDirty(void* nodePtr) {
    auto it = m_cache.find(nodePtr);
    if (it != m_cache.end()) {
        it->second.dirtyGeneration++;
    }
}

bool MatrixCache::getCached(void* nodePtr, float px, float py, float rot, float sx, float sy, AffineMatrix& outMatrix) {
    auto it = m_cache.find(nodePtr);
    if (it == m_cache.end()) {
        m_misses++;
        return false;
    }

    const auto& item = it->second;
    if (item.lastPosX == px && item.lastPosY == py &&
        item.lastRot == rot &&
        item.lastScaleX == sx && item.lastScaleY == sy) {
        outMatrix = item.matrix;
        m_hits++;
        return true;
    }

    m_misses++;
    return false;
}

void MatrixCache::store(void* nodePtr, float px, float py, float rot, float sx, float sy, const AffineMatrix& matrix) {
    CachedTransform ct;
    ct.matrix = matrix;
    ct.lastPosX = px;
    ct.lastPosY = py;
    ct.lastRot = rot;
    ct.lastScaleX = sx;
    ct.lastScaleY = sy;
    ct.dirtyGeneration = 0;
    m_cache[nodePtr] = ct;
}

} // namespace optigd::optimization

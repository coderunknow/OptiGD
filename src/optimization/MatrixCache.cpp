#include "MatrixCache.hpp"
#include <algorithm>
#include <cmath>

namespace optigd::optimization {

AffineMatrix AffineMatrix::fromNodeParams(float px, float py, float rotDegrees,
                                          float sx, float sy,
                                          float anchorInPointsX, float anchorInPointsY) {
    float c = 1.0f;
    float s = 0.0f;
    if (rotDegrees != 0.0f) {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        float radians = -rotDegrees * kDegToRad;
        c = std::cos(radians);
        s = std::sin(radians);
    }

    float x = px;
    float y = py;
    if (anchorInPointsX != 0.0f || anchorInPointsY != 0.0f) {
        x += c * -anchorInPointsX * sx + -s * -anchorInPointsY * sy;
        y += s * -anchorInPointsX * sx +  c * -anchorInPointsY * sy;
    }

    return {
        c * sx,
        s * sx,
        -s * sy,
        c * sy,
        x,
        y
    };
}

core::Rect2D AffineMatrix::transformRect(const core::Rect2D& localRect) const {
    core::Point2D p0 = transformPoint({localRect.minX(), localRect.minY()});
    core::Point2D p1 = transformPoint({localRect.maxX(), localRect.minY()});
    core::Point2D p2 = transformPoint({localRect.minX(), localRect.maxY()});
    core::Point2D p3 = transformPoint({localRect.maxX(), localRect.maxY()});

    float minX = std::min({p0.x, p1.x, p2.x, p3.x});
    float maxX = std::max({p0.x, p1.x, p2.x, p3.x});
    float minY = std::min({p0.y, p1.y, p2.y, p3.y});
    float maxY = std::max({p0.y, p1.y, p2.y, p3.y});

    return core::Rect2D(minX, minY, maxX - minX, maxY - minY);
}

MatrixCache& MatrixCache::instance() {
    static MatrixCache s_instance;
    return s_instance;
}

void MatrixCache::clear() {
    m_cache.clear();
    m_hits = 0;
    m_misses = 0;
}

void MatrixCache::markChildrenDirtyRecursive(void* nodePtr, uint32_t depth) {
    if (!nodePtr || depth > 32) return;
    auto it = m_cache.find(nodePtr);
    if (it == m_cache.end()) return;

    for (void* childPtr : it->second.children) {
        auto childIt = m_cache.find(childPtr);
        if (childIt != m_cache.end()) {
            childIt->second.dirty = true;
            childIt->second.dirtyGeneration++;
            markChildrenDirtyRecursive(childPtr, depth + 1);
        }
    }
}

void MatrixCache::markDirty(void* nodePtr) {
    if (!nodePtr) return;
    auto it = m_cache.find(nodePtr);
    if (it != m_cache.end()) {
        it->second.dirty = true;
        it->second.dirtyGeneration++;
        markChildrenDirtyRecursive(nodePtr, 0);
    }
}

void MatrixCache::evict(void* nodePtr) {
    if (!nodePtr) return;
    auto it = m_cache.find(nodePtr);
    if (it == m_cache.end()) return;

    // Detach from parent's child list if linked
    if (it->second.parentPtr != nullptr) {
        auto parentIt = m_cache.find(it->second.parentPtr);
        if (parentIt != m_cache.end()) {
            auto& siblings = parentIt->second.children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), nodePtr), siblings.end());
        }
    }

    // Invalidate and detach children
    for (void* childPtr : it->second.children) {
        auto childIt = m_cache.find(childPtr);
        if (childIt != m_cache.end()) {
            childIt->second.parentPtr = nullptr;
            childIt->second.dirty = true;
            childIt->second.dirtyGeneration++;
            markChildrenDirtyRecursive(childPtr, 1);
        }
    }

    m_cache.erase(it);
}

void MatrixCache::setParent(void* childPtr, void* parentPtr) {
    if (!childPtr || childPtr == parentPtr) return;
    auto& childEntry = m_cache[childPtr];
    if (childEntry.parentPtr == parentPtr) return;

    // Remove from old parent
    if (childEntry.parentPtr != nullptr) {
        auto oldParentIt = m_cache.find(childEntry.parentPtr);
        if (oldParentIt != m_cache.end()) {
            auto& ch = oldParentIt->second.children;
            ch.erase(std::remove(ch.begin(), ch.end(), childPtr), ch.end());
        }
    }

    childEntry.parentPtr = parentPtr;
    childEntry.dirty = true;
    childEntry.dirtyGeneration++;

    if (parentPtr != nullptr) {
        auto& parentEntry = m_cache[parentPtr];
        if (std::find(parentEntry.children.begin(), parentEntry.children.end(), childPtr) == parentEntry.children.end()) {
            parentEntry.children.push_back(childPtr);
        }
    }
    markChildrenDirtyRecursive(childPtr, 0);
}

bool MatrixCache::getCached(void* nodePtr, float px, float py, float rot, float sx, float sy, AffineMatrix& outMatrix) {
    if (!nodePtr) {
        m_misses++;
        return false;
    }

    auto it = m_cache.find(nodePtr);
    if (it == m_cache.end()) {
        m_misses++;
        return false;
    }

    const auto& item = it->second;
    // An entry created implicitly by setParent() holds a default identity
    // matrix that was never derived from real node parameters.
    if (item.dirty || !item.computed) {
        m_misses++;
        return false;
    }

    // If linked to a parent that is dirty, invalidate
    if (item.parentPtr != nullptr) {
        auto pIt = m_cache.find(item.parentPtr);
        if (pIt != m_cache.end() && pIt->second.dirty) {
            m_misses++;
            return false;
        }
    }

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

void MatrixCache::store(void* nodePtr, float px, float py, float rot, float sx, float sy,
                        const AffineMatrix& matrix, float anchorX, float anchorY) {
    if (!nodePtr) return;

    auto& ct = m_cache[nodePtr];
    // The anchor is part of the getOrCompute() cache key, so it must be part of
    // the change detection here too - otherwise a store() would leave a stale
    // anchor behind and getOrCompute() would compute an incorrect hit/miss.
    bool changed = ct.dirty || !ct.computed ||
                   ct.lastPosX != px || ct.lastPosY != py ||
                   ct.lastRot != rot ||
                   ct.lastScaleX != sx || ct.lastScaleY != sy ||
                   ct.lastAnchorX != anchorX || ct.lastAnchorY != anchorY;

    ct.matrix = matrix;
    ct.lastPosX = px;
    ct.lastPosY = py;
    ct.lastRot = rot;
    ct.lastScaleX = sx;
    ct.lastScaleY = sy;
    ct.lastAnchorX = anchorX;
    ct.lastAnchorY = anchorY;
    ct.computed = true;
    ct.dirty = false;

    if (changed) {
        markChildrenDirtyRecursive(nodePtr, 0);
    }
}

size_t MatrixCache::pruneStaleEntries() {
    size_t pruned = 0;
    bool removedAny = true;

    // Repeat until a full pass removes nothing: dropping a phantom leaf can
    // turn its parent into a childless phantom that is then removable too.
    while (removedAny) {
        removedAny = false;
        for (auto it = m_cache.begin(); it != m_cache.end();) {
            CachedTransform& ct = it->second;
            if (ct.computed || !ct.children.empty()) {
                ++it;
                continue;
            }

            // Detach from the parent first: erasing the entry without doing so
            // would leave the parent holding a dangling child pointer.
            if (ct.parentPtr != nullptr) {
                auto parentIt = m_cache.find(ct.parentPtr);
                if (parentIt != m_cache.end()) {
                    auto& siblings = parentIt->second.children;
                    siblings.erase(std::remove(siblings.begin(), siblings.end(), it->first),
                                   siblings.end());
                }
            }

            it = m_cache.erase(it);
            ++pruned;
            removedAny = true;
        }
    }
    return pruned;
}

AffineMatrix MatrixCache::getOrCompute(void* nodePtr, float px, float py, float rot,
                                       float sx, float sy,
                                       float anchorX, float anchorY) {
    AffineMatrix mat;
    if (nodePtr != nullptr) {
        auto it = m_cache.find(nodePtr);
        if (it != m_cache.end() && !it->second.dirty &&
            it->second.lastPosX == px && it->second.lastPosY == py &&
            it->second.lastRot == rot &&
            it->second.lastScaleX == sx && it->second.lastScaleY == sy &&
            it->second.lastAnchorX == anchorX && it->second.lastAnchorY == anchorY) {
            m_hits++;
            return it->second.matrix;
        }
    }

    m_misses++;
    mat = AffineMatrix::fromNodeParams(px, py, rot, sx, sy, anchorX, anchorY);
    if (nodePtr != nullptr) {
        store(nodePtr, px, py, rot, sx, sy, mat, anchorX, anchorY);
    }
    return mat;
}

} // namespace optigd::optimization

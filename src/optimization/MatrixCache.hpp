#pragma once

#include "../core/Types.hpp"
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <vector>
#include <cmath>

namespace optigd::optimization {

// 3x2 Affine transform representation (matching cocos2d::CCAffineTransform)
struct AffineMatrix {
    float a{1.0f}, b{0.0f}, c{0.0f}, d{1.0f};
    float tx{0.0f}, ty{0.0f};

    static AffineMatrix identity() {
        return {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    }

    // Computes standard Cocos2d-x nodeToParentTransform matrix
    static AffineMatrix fromNodeParams(float px, float py, float rotDegrees,
                                       float sx, float sy,
                                       float anchorInPointsX = 0.0f,
                                       float anchorInPointsY = 0.0f);

    AffineMatrix concat(const AffineMatrix& t2) const {
        return {
            a * t2.a + b * t2.c,
            a * t2.b + b * t2.d,
            c * t2.a + d * t2.c,
            c * t2.b + d * t2.d,
            tx * t2.a + ty * t2.c + t2.tx,
            tx * t2.b + ty * t2.d + t2.ty
        };
    }

    core::Point2D transformPoint(const core::Point2D& pt) const {
        return {
            a * pt.x + c * pt.y + tx,
            b * pt.x + d * pt.y + ty
        };
    }

    core::Rect2D transformRect(const core::Rect2D& localRect) const;

    bool approximatelyEquals(const AffineMatrix& other, float epsilon = 1e-5f) const {
        return std::abs(a - other.a) <= epsilon &&
               std::abs(b - other.b) <= epsilon &&
               std::abs(c - other.c) <= epsilon &&
               std::abs(d - other.d) <= epsilon &&
               std::abs(tx - other.tx) <= epsilon &&
               std::abs(ty - other.ty) <= epsilon;
    }
};

struct CachedTransform {
    AffineMatrix matrix{AffineMatrix::identity()};
    uint32_t dirtyGeneration{0};
    bool dirty{false};
    float lastPosX{0.0f};
    float lastPosY{0.0f};
    float lastRot{0.0f};
    float lastScaleX{1.0f};
    float lastScaleY{1.0f};
    float lastAnchorX{0.0f};
    float lastAnchorY{0.0f};
    void* parentPtr{nullptr};
    std::vector<void*> children;
};

class MatrixCache {
public:
    static MatrixCache& instance();

    void clear();
    void markDirty(void* nodePtr);
    void evict(void* nodePtr);
    void setParent(void* childPtr, void* parentPtr);

    bool getCached(void* nodePtr, float px, float py, float rot, float sx, float sy, AffineMatrix& outMatrix);
    void store(void* nodePtr, float px, float py, float rot, float sx, float sy, const AffineMatrix& matrix);

    // Computes or retrieves cached local transform
    AffineMatrix getOrCompute(void* nodePtr, float px, float py, float rot,
                              float sx, float sy,
                              float anchorX = 0.0f, float anchorY = 0.0f);

    size_t getCachedCount() const { return m_cache.size(); }
    uint64_t getHits() const { return m_hits; }
    uint64_t getMisses() const { return m_misses; }

    void resetStats() {
        m_hits = 0;
        m_misses = 0;
    }

private:
    MatrixCache() = default;
    std::unordered_map<void*, CachedTransform> m_cache;
    uint64_t m_hits{0};
    uint64_t m_misses{0};

    void markChildrenDirtyRecursive(void* nodePtr, uint32_t depth = 0);
};

} // namespace optigd::optimization

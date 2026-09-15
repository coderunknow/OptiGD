#pragma once

#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <array>

namespace optigd::optimization {

// 3x2 Affine transform representation (matching CCAffineTransform)
struct AffineMatrix {
    float a{1.0f}, b{0.0f}, c{0.0f}, d{1.0f};
    float tx{0.0f}, ty{0.0f};

    static AffineMatrix identity() {
        return {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    }

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
};

struct CachedTransform {
    AffineMatrix matrix{AffineMatrix::identity()};
    uint32_t dirtyGeneration{0};
    float lastPosX{0.0f};
    float lastPosY{0.0f};
    float lastRot{0.0f};
    float lastScaleX{1.0f};
    float lastScaleY{1.0f};
};

class MatrixCache {
public:
    static MatrixCache& instance();

    void clear();
    void markDirty(void* nodePtr);

    bool getCached(void* nodePtr, float px, float py, float rot, float sx, float sy, AffineMatrix& outMatrix);
    void store(void* nodePtr, float px, float py, float rot, float sx, float sy, const AffineMatrix& matrix);

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
};

} // namespace optigd::optimization

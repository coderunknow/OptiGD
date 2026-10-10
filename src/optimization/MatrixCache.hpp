#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace optigd::optimization {

// 2x3 affine transform in the same layout as cocos2d's CCAffineTransform.
struct AffineMatrix {
    float a{1.0f}, b{0.0f}, c{0.0f}, d{1.0f};
    float tx{0.0f}, ty{0.0f};

    static AffineMatrix identity() {
        return {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    }

    // Returns this * t2 (apply this, then t2), matching cocos2d's CCAffineTransformConcat(t1, t2).
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

    bool operator==(const AffineMatrix& o) const {
        return a == o.a && b == o.b && c == o.c && d == o.d && tx == o.tx && ty == o.ty;
    }
};

// Local (node-to-parent) transform inputs. The key used for validation is the full
// set of inputs, compared exactly.
struct LocalTransformInputs {
    float posX{0.0f};
    float posY{0.0f};
    float rotation{0.0f};
    float scaleX{1.0f};
    float scaleY{1.0f};

    bool operator==(const LocalTransformInputs& o) const {
        return posX == o.posX && posY == o.posY && rotation == o.rotation &&
               scaleX == o.scaleX && scaleY == o.scaleY;
    }
};

// Caches LOCAL node transforms, keyed by node pointer.
//
// Validity rules:
//  - A lookup hits only if the entry is valid, the stored inputs equal the current
//    inputs exactly, and the entry has not been invalidated since it was stored.
//  - markDirty(node) and invalidate(node) must be called when a node's transform
//    changes or the node is destroyed. Pointers may be reused after destruction; an
//    entry must never outlive its node.
//  - Parent transforms are NOT cached. Callers compose the cached local matrix with
//    the parent's matrix each time (see tests for equivalence with uncached results).
//
// Status in v0.1.0-beta1: DISABLED. Cocos2d already caches node transforms through
// CCNode::m_bTransformDirty, so this cache has no demonstrated benefit. It is kept
// with verified semantics for future use.
class MatrixCache {
public:
    void clear();
    void markDirty(void* node);   // bumps the generation; the entry stops validating
    void invalidate(void* node);  // removes the entry (use when the node is destroyed)

    bool getCached(void* node, const LocalTransformInputs& inputs, AffineMatrix& outMatrix);
    void store(void* node, const LocalTransformInputs& inputs, const AffineMatrix& matrix);

    size_t getCachedCount() const { return m_cache.size(); }
    uint64_t getHits() const { return m_hits; }
    uint64_t getMisses() const { return m_misses; }
    void resetStats() {
        m_hits = 0;
        m_misses = 0;
    }

private:
    struct Entry {
        LocalTransformInputs inputs{};
        AffineMatrix matrix{AffineMatrix::identity()};
        uint32_t generation{0};
        bool valid{false};
    };

    std::unordered_map<void*, Entry> m_cache;
    uint64_t m_hits{0};
    uint64_t m_misses{0};
};

} // namespace optigd::optimization

#include "MatrixCache.hpp"

namespace optigd::optimization {

void MatrixCache::clear() {
    m_cache.clear();
    m_hits = 0;
    m_misses = 0;
}

void MatrixCache::markDirty(void* node) {
    auto it = m_cache.find(node);
    if (it != m_cache.end()) {
        it->second.generation++;
        it->second.valid = false;
    }
}

void MatrixCache::invalidate(void* node) {
    m_cache.erase(node);
}

bool MatrixCache::getCached(void* node, const LocalTransformInputs& inputs, AffineMatrix& outMatrix) {
    auto it = m_cache.find(node);
    if (it == m_cache.end() || !it->second.valid || !(it->second.inputs == inputs)) {
        m_misses++;
        return false;
    }
    outMatrix = it->second.matrix;
    m_hits++;
    return true;
}

void MatrixCache::store(void* node, const LocalTransformInputs& inputs, const AffineMatrix& matrix) {
    Entry& entry = m_cache[node];
    entry.inputs = inputs;
    entry.matrix = matrix;
    entry.valid = true;
    // generation is kept so that generation numbers only increase for a given node.
}

} // namespace optigd::optimization

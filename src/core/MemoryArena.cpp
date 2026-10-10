#include "MemoryArena.hpp"
#include <cstdlib>
#include <new>
#include <algorithm>
#include <limits>

namespace optigd::core {

size_t MemoryArena::normalizeAlignment(size_t alignment) {
    if (alignment == 0) {
        return alignof(std::max_align_t);
    }
    if ((alignment & (alignment - 1)) == 0) {
        return alignment;
    }
    // Round up to next power of two
    size_t pow2 = 1;
    while (pow2 < alignment && pow2 <= (std::numeric_limits<size_t>::max() >> 1)) {
        pow2 <<= 1;
    }
    return pow2;
}

MemoryArena::Block::Block(size_t s) : size(s), offset(0) {
    memory = static_cast<uint8_t*>(std::malloc(size));
    if (!memory) {
        throw std::bad_alloc();
    }
}

MemoryArena::Block::~Block() {
    if (memory) {
        std::free(memory);
        memory = nullptr;
    }
}

MemoryArena::Block::Block(Block&& other) noexcept
    : memory(other.memory), size(other.size), offset(other.offset) {
    other.memory = nullptr;
    other.size = 0;
    other.offset = 0;
}

MemoryArena::Block& MemoryArena::Block::operator=(Block&& other) noexcept {
    if (this != &other) {
        if (memory) {
            std::free(memory);
        }
        memory = other.memory;
        size = other.size;
        offset = other.offset;
        other.memory = nullptr;
        other.size = 0;
        other.offset = 0;
    }
    return *this;
}

void* MemoryArena::Block::allocate(size_t bytes, size_t alignment, size_t& outAlignedConsumed) {
    if (offset > size || bytes > size - offset) {
        return nullptr;
    }

    uintptr_t currentPtr = reinterpret_cast<uintptr_t>(memory + offset);
    uintptr_t mask = static_cast<uintptr_t>(alignment - 1);
    if (currentPtr > std::numeric_limits<uintptr_t>::max() - mask) {
        return nullptr;
    }
    uintptr_t alignedPtr = (currentPtr + mask) & ~mask;
    size_t padding = static_cast<size_t>(alignedPtr - currentPtr);

    if (padding > size - offset || bytes > size - offset - padding) {
        return nullptr;
    }

    outAlignedConsumed = padding + bytes;
    offset += outAlignedConsumed;
    return reinterpret_cast<void*>(alignedPtr);
}

void MemoryArena::Block::reset() {
    offset = 0;
}

MemoryArena::MemoryArena(size_t defaultBlockSize)
    : m_defaultBlockSize(std::max<size_t>(defaultBlockSize, 64)) {
    allocateNewBlock(m_defaultBlockSize);
}

MemoryArena::~MemoryArena() = default;

MemoryArena::MemoryArena(MemoryArena&&) noexcept = default;
MemoryArena& MemoryArena::operator=(MemoryArena&&) noexcept = default;

void MemoryArena::allocateNewBlock(size_t minSize) {
    size_t blockSize = std::max(m_defaultBlockSize, minSize);
    if (!m_blocks.empty()) {
        m_expansionCount++;
    }
    m_blocks.emplace_back(blockSize);
    m_totalCapacity += blockSize;
}

void* MemoryArena::allocate(size_t bytes, size_t alignment) {
    if (bytes == 0) {
        return nullptr;
    }

    size_t normAlign = normalizeAlignment(alignment);
    // Reject impossible overflow sizes (> 1 GB single scratch request or size_t wrap)
    constexpr size_t kMaxSingleAllocation = static_cast<size_t>(1) << 30;
    if (bytes > kMaxSingleAllocation || normAlign > kMaxSingleAllocation ||
        bytes > std::numeric_limits<size_t>::max() - normAlign) {
        return nullptr;
    }

    size_t alignedConsumed = 0;

    if (m_currentBlockIndex < m_blocks.size()) {
        void* ptr = m_blocks[m_currentBlockIndex].allocate(bytes, normAlign, alignedConsumed);
        if (ptr) {
            m_allocationCount++;
            m_usedBytes += bytes;
            m_alignedUsedBytes += alignedConsumed;
            m_peakUsedBytes = std::max(m_peakUsedBytes, m_alignedUsedBytes);
            return ptr;
        }
    }

    // Try subsequent existing blocks if available. Their contents are left
    // untouched: wiping them would silently invalidate live frame-scoped data.
    for (size_t i = m_currentBlockIndex + 1; i < m_blocks.size(); ++i) {
        m_currentBlockIndex = i;
        void* ptr = m_blocks[i].allocate(bytes, normAlign, alignedConsumed);
        if (ptr) {
            m_allocationCount++;
            m_usedBytes += bytes;
            m_alignedUsedBytes += alignedConsumed;
            m_peakUsedBytes = std::max(m_peakUsedBytes, m_alignedUsedBytes);
            return ptr;
        }
    }

    // Allocate a new block
    allocateNewBlock(bytes + normAlign);
    m_currentBlockIndex = m_blocks.size() - 1;
    void* ptr = m_blocks[m_currentBlockIndex].allocate(bytes, normAlign, alignedConsumed);
    if (ptr) {
        m_allocationCount++;
        m_usedBytes += bytes;
        m_alignedUsedBytes += alignedConsumed;
        m_peakUsedBytes = std::max(m_peakUsedBytes, m_alignedUsedBytes);
    }
    return ptr;
}

void MemoryArena::reset() {
    for (auto& block : m_blocks) {
        block.reset();
    }
    m_currentBlockIndex = 0;
    m_usedBytes = 0;
    m_alignedUsedBytes = 0;
    m_allocationCount = 0;
}

} // namespace optigd::core

#include "MemoryArena.hpp"

#include <algorithm>

namespace optigd::core {

MemoryArena::MemoryArena(size_t defaultBlockSize)
    : m_defaultBlockSize(std::max<size_t>(defaultBlockSize, 64)) {
    addBlock(m_defaultBlockSize);
}

bool MemoryArena::addBlock(size_t minBytes) {
    size_t size = std::max(m_defaultBlockSize, minBytes);
    if (size > kMaxBlockBytes) {
        return false;
    }
    Block block;
    try {
        block.memory = std::make_unique<uint8_t[]>(size);
    } catch (const std::bad_alloc&) {
        return false;
    }
    block.size = size;
    block.offset = 0;
    m_blocks.push_back(std::move(block));
    m_totalCapacity += size;
    m_heapBlockAllocations++;
    return true;
}

void* MemoryArena::tryBlock(size_t index, size_t bytes, size_t alignment) {
    Block& block = m_blocks[index];
    const uintptr_t base = reinterpret_cast<uintptr_t>(block.memory.get());
    const uintptr_t current = base + block.offset;
    // Alignment is validated by the caller (power of two), so this mask is exact.
    const uintptr_t aligned = (current + (alignment - 1)) & ~static_cast<uintptr_t>(alignment - 1);
    const size_t padding = static_cast<size_t>(aligned - current);

    const size_t remaining = block.size - block.offset; // offset <= size always
    if (padding > remaining) {
        return nullptr;
    }
    if (bytes > remaining - padding) {
        return nullptr;
    }

    const size_t consumed = padding + bytes;
    block.offset += consumed;
    m_usedBytes += consumed;
    return reinterpret_cast<void*>(aligned);
}

void* MemoryArena::allocate(size_t bytes, size_t alignment) {
    // Reject non-power-of-two (and zero) alignments instead of producing misaligned pointers.
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return nullptr;
    }
    // Requests that cannot fit in any legal block are rejected before any arithmetic.
    if (bytes > kMaxBlockBytes || alignment > kMaxBlockBytes) {
        return nullptr;
    }

    for (size_t i = m_currentBlockIndex; i < m_blocks.size(); ++i) {
        if (void* ptr = tryBlock(i, bytes, alignment)) {
            m_currentBlockIndex = i;
            m_allocationCount++;
            return ptr;
        }
    }

    // No existing block fits: grow. A fresh block of at least bytes + alignment - 1
    // always fits the request at offset 0.
    const size_t needed = bytes + (alignment - 1);
    if (!addBlock(needed)) {
        return nullptr;
    }
    m_currentBlockIndex = m_blocks.size() - 1;
    void* ptr = tryBlock(m_currentBlockIndex, bytes, alignment);
    if (ptr) {
        m_allocationCount++;
    }
    return ptr;
}

void MemoryArena::reset() {
    for (auto& block : m_blocks) {
        block.offset = 0;
    }
    m_currentBlockIndex = 0;
    m_usedBytes = 0;
    m_allocationCount = 0;
}

} // namespace optigd::core

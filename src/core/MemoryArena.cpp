#include "MemoryArena.hpp"
#include <cstdlib>
#include <new>
#include <algorithm>

namespace optigd::core {

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

void* MemoryArena::Block::allocate(size_t bytes, size_t alignment) {
    uintptr_t currentPtr = reinterpret_cast<uintptr_t>(memory + offset);
    uintptr_t alignedPtr = (currentPtr + (alignment - 1)) & ~(alignment - 1);
    size_t padding = alignedPtr - currentPtr;

    if (offset + padding + bytes > size) {
        return nullptr;
    }

    offset += padding + bytes;
    return reinterpret_cast<void*>(alignedPtr);
}

void MemoryArena::Block::reset() {
    offset = 0;
}

MemoryArena::MemoryArena(size_t defaultBlockSize)
    : m_defaultBlockSize(defaultBlockSize) {
    allocateNewBlock(m_defaultBlockSize);
}

MemoryArena::~MemoryArena() = default;

MemoryArena::MemoryArena(MemoryArena&&) noexcept = default;
MemoryArena& MemoryArena::operator=(MemoryArena&&) noexcept = default;

void MemoryArena::allocateNewBlock(size_t minSize) {
    size_t blockSize = std::max(m_defaultBlockSize, minSize);
    m_blocks.emplace_back(blockSize);
    m_totalCapacity += blockSize;
}

void* MemoryArena::allocate(size_t bytes, size_t alignment) {
    m_allocationCount++;

    if (m_currentBlockIndex < m_blocks.size()) {
        void* ptr = m_blocks[m_currentBlockIndex].allocate(bytes, alignment);
        if (ptr) {
            m_usedBytes += bytes;
            return ptr;
        }
    }

    // Try subsequent existing blocks if available
    for (size_t i = m_currentBlockIndex + 1; i < m_blocks.size(); ++i) {
        m_currentBlockIndex = i;
        m_blocks[i].reset();
        void* ptr = m_blocks[i].allocate(bytes, alignment);
        if (ptr) {
            m_usedBytes += bytes;
            return ptr;
        }
    }

    // Allocate a new block
    allocateNewBlock(bytes + alignment);
    m_currentBlockIndex = m_blocks.size() - 1;
    void* ptr = m_blocks[m_currentBlockIndex].allocate(bytes, alignment);
    m_usedBytes += bytes;
    return ptr;
}

void MemoryArena::reset() {
    for (auto& block : m_blocks) {
        block.reset();
    }
    m_currentBlockIndex = 0;
    m_usedBytes = 0;
    m_allocationCount = 0;
}

} // namespace optigd::core

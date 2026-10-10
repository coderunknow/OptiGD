#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace optigd::core {

// Linear (bump) allocator for transient scratch memory.
//
// Semantics:
//  - allocate() returns nullptr for an invalid request (alignment not a power of
//    two, size arithmetic overflow, block larger than kMaxBlockBytes) or when the
//    system refuses memory. It never returns a misaligned pointer.
//  - reset() rewinds every block to offset 0 and keeps the memory, so the
//    steady-state per-frame path performs no heap allocation.
//  - Blocks are only obtained from the heap when the existing blocks cannot
//    satisfy a request. getHeapBlockAllocations() counts those calls.
//  - Objects created with create() are not destroyed by the arena; only
//    trivially destructible types should be stored.
//  - Not thread-safe.
class MemoryArena {
public:
    static constexpr size_t kMaxBlockBytes = size_t{1} << 30; // 1 GiB guard

    explicit MemoryArena(size_t defaultBlockSize = 1024 * 1024);
    ~MemoryArena() = default;

    MemoryArena(const MemoryArena&) = delete;
    MemoryArena& operator=(const MemoryArena&) = delete;
    MemoryArena(MemoryArena&&) noexcept = default;
    MemoryArena& operator=(MemoryArena&&) noexcept = default;

    void* allocate(size_t bytes, size_t alignment = alignof(std::max_align_t));

    template <typename T, typename... Args>
    T* create(Args&&... args) {
        void* ptr = allocate(sizeof(T), alignof(T));
        if (!ptr) return nullptr;
        return ::new (ptr) T(std::forward<Args>(args)...);
    }

    template <typename T>
    T* allocateArray(size_t count) {
        if (count > SIZE_MAX / sizeof(T)) return nullptr;
        return static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
    }

    // Rewinds all blocks. Previously returned pointers become invalid.
    void reset();

    // Bytes consumed since the last reset, including alignment padding.
    size_t getUsedBytes() const { return m_usedBytes; }
    // Total bytes owned by the arena (sum of block sizes).
    size_t getCapacity() const { return m_totalCapacity; }
    // Number of successful allocate() calls since the last reset.
    size_t getAllocationCount() const { return m_allocationCount; }
    // Number of blocks obtained from the heap over the arena's lifetime.
    size_t getHeapBlockAllocations() const { return m_heapBlockAllocations; }
    size_t getBlockCount() const { return m_blocks.size(); }

private:
    struct Block {
        std::unique_ptr<uint8_t[]> memory;
        size_t size{0};
        size_t offset{0};
    };

    // Attempts to satisfy the request inside blocks[index]. Returns nullptr if it does not fit.
    void* tryBlock(size_t index, size_t bytes, size_t alignment);
    bool addBlock(size_t minBytes);

    size_t m_defaultBlockSize;
    std::vector<Block> m_blocks;
    size_t m_currentBlockIndex{0};
    size_t m_usedBytes{0};
    size_t m_totalCapacity{0};
    size_t m_allocationCount{0};
    size_t m_heapBlockAllocations{0};
};

} // namespace optigd::core

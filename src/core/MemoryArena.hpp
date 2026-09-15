#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <cassert>

namespace optigd::core {

class MemoryArena {
public:
    explicit MemoryArena(size_t defaultBlockSize = 1024 * 1024);
    ~MemoryArena();

    MemoryArena(const MemoryArena&) = delete;
    MemoryArena& operator=(const MemoryArena&) = delete;
    MemoryArena(MemoryArena&&) noexcept;
    MemoryArena& operator=(MemoryArena&&) noexcept;

    void* allocate(size_t bytes, size_t alignment = alignof(std::max_align_t));

    template <typename T, typename... Args>
    T* create(Args&&... args) {
        void* ptr = allocate(sizeof(T), alignof(T));
        return ::new (ptr) T(std::forward<Args>(args)...);
    }

    template <typename T>
    T* allocateArray(size_t count) {
        return static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
    }

    void reset();

    size_t getUsedBytes() const { return m_usedBytes; }
    size_t getCapacity() const { return m_totalCapacity; }
    size_t getAllocationCount() const { return m_allocationCount; }

private:
    struct Block {
        uint8_t* memory{nullptr};
        size_t size{0};
        size_t offset{0};

        explicit Block(size_t s);
        ~Block();

        Block(const Block&) = delete;
        Block& operator=(const Block&) = delete;
        Block(Block&& other) noexcept;
        Block& operator=(Block&& other) noexcept;

        void* allocate(size_t bytes, size_t alignment);
        void reset();
    };

    size_t m_defaultBlockSize;
    std::vector<Block> m_blocks;
    size_t m_currentBlockIndex{0};
    size_t m_usedBytes{0};
    size_t m_totalCapacity{0};
    size_t m_allocationCount{0};

    void allocateNewBlock(size_t minSize);
};

} // namespace optigd::core

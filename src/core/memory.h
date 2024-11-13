#pragma once

#include "common.h"
#include "core_math.h"
#include <cstring>
#include <memory>

namespace wb {
    
void* allocate_virtual(size_t size) noexcept;
void free_virtual(void* ptr, size_t size) noexcept;
uint32_t get_virtual_page_size() noexcept;

inline void* allocate_aligned(size_t size, size_t alignment = 16) noexcept {
#ifdef WB_PLATFORM_WINDOWS
    return _aligned_malloc(size, alignment);
#else
    return std::aligned_alloc(alignment, size);
#endif
}

inline void free_aligned(void* ptr) noexcept {
#ifdef WB_PLATFORM_WINDOWS
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

struct PoolHeader {
    PoolHeader* next_pool;
};

struct PoolChunk {
    PoolChunk* next_chunk;
};

// Growable pool
template <typename T>
struct Pool {
    static constexpr size_t block_size = 65536;
    static constexpr size_t alloc_size = math::max(sizeof(T), sizeof(PoolHeader));
    static constexpr size_t alloc_alignment = math::max(alignof(T), sizeof(PoolHeader));
    static constexpr size_t aligned_size =
        alloc_size + (alloc_alignment - alloc_size % alloc_alignment) % alloc_alignment;
    static constexpr size_t objects_per_block = block_size / aligned_size;

    PoolHeader* first_pool = nullptr;
    PoolHeader* current_pool = nullptr;
    PoolChunk* current_allocation = nullptr;
    PoolChunk* last_allocation = nullptr;
    size_t num_reserved = 0;
    size_t num_allocated = 0;

    ~Pool() {
        PoolHeader* pool = first_pool;
        while (pool != nullptr) {
            PoolHeader* next_pool = pool->next_pool;
            free_aligned(pool);
            pool = next_pool;
        }
    }

    void* allocate() noexcept {
        if (num_allocated >= num_reserved) {
            if (!_reserve_new_block()) {
                return nullptr;
            }
        }

        PoolChunk* free_chunk = current_allocation;
        // last_allocation = current_allocation;
        current_allocation = current_allocation->next_chunk;
        ++num_allocated;

        return free_chunk;
    }

    void free(void* ptr) noexcept {
        std::memset(ptr, 0, alloc_size);
        PoolChunk* chunk = reinterpret_cast<PoolChunk*>(ptr);
        chunk->next_chunk = current_allocation;
        current_allocation = chunk;
        --num_allocated;
    }

    bool _reserve_new_block() noexcept {
        // One extra storage is required for the pool header. It may waste space a bit if the object
        // is too large. 
        std::size_t size = aligned_size * (objects_per_block + 1);
        uint8_t* pool = (uint8_t*)allocate_aligned(size, alloc_alignment);

        if (pool == nullptr)
            return false;

        std::memset(pool, 0, size);

        // Initialize the pool header
        PoolHeader* pool_header = reinterpret_cast<PoolHeader*>(pool);

        if (current_pool)
            current_pool->next_pool = pool_header;
        else
            first_pool = pool_header;

        current_pool = pool_header;
        pool_header->next_pool = nullptr;

        // Initialize free list.
        PoolChunk* current_chunk = reinterpret_cast<PoolChunk*>(pool + aligned_size);
        current_allocation = current_chunk;

        for (std::size_t i = 1; i < objects_per_block; i++) {
            current_chunk->next_chunk = reinterpret_cast<PoolChunk*>(
                reinterpret_cast<uint8_t*>(current_chunk) + aligned_size);
            current_chunk = current_chunk->next_chunk;
        }

        num_reserved += objects_per_block;
        return true;
    }
};

} // namespace wb
#pragma once

#include "../types.h"
#include "../stdpch.h"

namespace wb
{
    // Use this only for trivial types
    template<typename T, size_t LocalBufferSize>
        requires std::is_trivial_v<T>
    struct LocalQueue
    {
        T local_storage[LocalBufferSize]{};
        size_t capacity = LocalBufferSize;
        T* data;
        T* read_ptr;
        T* write_ptr;

        LocalQueue() :
            data(local_storage),
            read_ptr(data),
            write_ptr(data)
        {
        }

        ~LocalQueue()
        {
            if (data != local_storage)
                std::free(data);
        }

        inline bool push(const T& value)
        {
            if (num_items_written() == capacity)
                if (!reserve(capacity + (capacity / 2)))
                    return false;

            *write_ptr = value;
            write_ptr++;

            return true;
        }

        inline bool push(T&& value)
        {
            if (num_items_written() == capacity)
                if (!reserve(capacity + (capacity / 2)))
                    return false;

            *write_ptr = std::move(value);
            write_ptr++;

            return true;
        }

        inline T* pop() noexcept
        {
            if (write_ptr == read_ptr)
                return nullptr;

            return read_ptr++;
        }

        inline T* pop_all() noexcept
        {
            T* tmp = read_ptr;
            read_ptr = write_ptr;
            return tmp;
        }

        inline T& front() { return *read_ptr; }
        inline const T& front() const { return *read_ptr; }
        inline T& back() { return *write_ptr; }
        inline const T& back() const { return *write_ptr; }

        inline size_t size() const noexcept
        {
            return write_ptr - read_ptr;
        }

        inline size_t num_items_written() const noexcept
        {
            return write_ptr - data;
        }

        inline size_t num_items_read() const noexcept
        {
            return read_ptr - data;
        }

        inline bool empty() const noexcept
        {
            return size() == 0;
        }

        inline void clear() noexcept
        {
            write_ptr = data;
            read_ptr = data;
        }

        bool reserve(size_t n) noexcept
        {
            if (n > capacity) {
                T* new_storage = (T*)std::malloc(n * sizeof(T));

                if (new_storage == nullptr)
                    return false;

                std::memcpy(new_storage, data, num_items_written() * sizeof(T));

                if (data != local_storage)
                    std::free(data);

                data = new_storage;
                read_ptr = new_storage + num_items_read();
                write_ptr = new_storage + num_items_written();
                capacity = n;
            }

            return true;
        }
    };

    template<typename T, uint32_t Size>
        requires std::is_trivial_v<T>
    struct ConcurrentRingBuffer
    {
        T                       data[Size];
        std::atomic_uint32_t    read_pos;
        std::atomic_uint32_t    write_pos;
        std::mutex              mtx_;

        inline bool push(const T& value) noexcept
        {
            uint32_t pos = write_pos.load(std::memory_order_acquire);
            uint32_t current_read_pos = read_pos.load(std::memory_order_acquire);
            uint32_t new_pos = (pos + 1) % Size;

            while (!write_pos.compare_exchange_weak(current_read_pos, new_pos,
                                                    std::memory_order_release,
                                                    std::memory_order_relaxed));

            data[pos] = value;
            return true;
        }

        inline T* pop() noexcept
        {
            uint32_t pos = read_pos.load(std::memory_order_acquire);
            if (pos == write_pos.load(std::memory_order_acquire))
                return nullptr;
            read_pos.store((pos + 1) % Size, std::memory_order_release);
            return data + pos;
        }

        inline bool empty() noexcept
        {
            return read_pos.load(std::memory_order_relaxed) == write_pos.load(std::memory_order_relaxed);
        }
    };
}
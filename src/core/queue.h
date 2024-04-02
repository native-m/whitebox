#pragma once

#include "core/common.h"
#include <type_traits>

namespace wb {

template <typename T, size_t LocalBufferSize>
    requires std::is_trivial_v<T>
struct LocalQueue {
    T local_storage[LocalBufferSize] {};
    size_t capacity = LocalBufferSize;
    T* data;
    T* read_ptr;
    T* write_ptr;

    LocalQueue() : data(local_storage), read_ptr(data), write_ptr(data) {}

    ~LocalQueue() {
        if (data != local_storage)
            std::free(data);
    }

    inline bool push(const T& value) {
        if (num_items_written() == capacity)
            if (!reserve(capacity + (capacity / 2)))
                return false;

        *write_ptr = value;
        write_ptr++;

        return true;
    }

    inline bool push(T&& value) {
        if (num_items_written() == capacity)
            if (!reserve(capacity + (capacity / 2)))
                return false;

        *write_ptr = std::move(value);
        write_ptr++;

        return true;
    }

    inline T* pop() noexcept {
        if (write_ptr == read_ptr)
            return nullptr;

        return read_ptr++;
    }

    inline T* pop_all() noexcept {
        T* tmp = read_ptr;
        read_ptr = write_ptr;
        return tmp;
    }

    inline T& front() { return *read_ptr; }
    inline const T& front() const { return *read_ptr; }
    inline T& back() { return *write_ptr; }
    inline const T& back() const { return *write_ptr; }
    inline size_t size() const noexcept { return write_ptr - read_ptr; }
    inline size_t num_items_written() const noexcept { return write_ptr - data; }
    inline size_t num_items_read() const noexcept { return read_ptr - data; }
    inline bool empty() const noexcept { return size() == 0; }

    inline void clear() noexcept {
        write_ptr = data;
        read_ptr = data;
    }

    bool reserve(size_t n) noexcept {
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

} // namespace wb

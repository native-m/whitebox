#pragma once

#include "common.h"
#include "thread.h"
#include "types.h"

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

template <Trivial T>
struct RingBuffer {
    uint32_t write_pos_ {};
    uint32_t read_pos_ {};
    uint32_t capacity_ {};
    uint32_t size_ {};
    T* data_ {};

    RingBuffer(uint32_t capacity) : capacity_ {capacity} {
        data_ = (T*)std::malloc(sizeof(T) * capacity_);
        assert(data_ && "Cannot allocate ring buffer");
    }

    void push(const T& value) {
        data_[write_pos_] = value;
        write_pos_ = (write_pos_ + 1) % capacity_;
        size_++;
    }

    T* pop(const T& value) {
        uint32_t current_pos = read_pos_;
        read_pos_ = (read_pos_ + 1) % capacity_;
        size_++;
        return data_[current_pos];
    }
};

template <Trivial T>
struct ConcurrentRingBuffer {
    static constexpr auto cache_size_ = 64;

    alignas(cache_size_) std::atomic<uint32_t> write_pos_ {};
    alignas(cache_size_) std::atomic<uint32_t> read_pos_ {};
    T* data_ {};
    uint32_t capacity_ {};

    ~ConcurrentRingBuffer() {
        if (data_)
            std::free(data_);
    }

    // Not thread-safe
    void set_capacity(size_t capacity) {
        T* new_data = (T*)std::malloc(sizeof(T) * capacity);
        assert(new_data != nullptr && "Cannot allocate memory");
        if (data_)
            std::free(data_);
        data_ = new_data;
        capacity_ = capacity;
    }

    inline bool try_push(const T& value) {
        uint32_t write_pos = write_pos_.load(std::memory_order_relaxed);
        uint32_t read_pos = read_pos_.load(std::memory_order_acquire);
        uint32_t next_write_pos = (write_pos + 1) % capacity_;

        if (next_write_pos == read_pos)
            return false;

        data_[write_pos] = value;
        write_pos_.store(next_write_pos, std::memory_order_release);
        return true;
    }

    inline void push(const T& value) {
        while (!try_push(value))
            std::this_thread::yield();
    }

    inline bool pop(T& value) {
        uint32_t write_pos = write_pos_.load(std::memory_order_acquire);
        uint32_t read_pos = read_pos_.load(std::memory_order_relaxed);

        if (write_pos == read_pos)
            return false;

        value = data_[read_pos];
        read_pos = (read_pos + 1) % capacity_;
        read_pos_.store(read_pos, std::memory_order_release);
        return true;
    }
};

} // namespace wb

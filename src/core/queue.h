#pragma once

#include "core/common.h"
#include "core/thread.h"
#include "core/types.h"

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
    static constexpr auto cache_size_ = std::hardware_destructive_interference_size;

    alignas(cache_size_) std::atomic<uint32_t> write_pos_ {};
    alignas(cache_size_) std::atomic<uint32_t> read_pos_ {};
    alignas(cache_size_) std::atomic<uint32_t> size_ {};
    uint32_t capacity_ {};
    T* data_ {};
    Spinlock resize_lock_;

    inline void push(const T& value) {
        uint32_t write_pos = write_pos_.load(std::memory_order_relaxed);
        uint32_t read_pos = read_pos_.load(std::memory_order_acquire);
        uint32_t size = size_.load(std::memory_order_relaxed) + 1;
        if (size >= capacity_) {
            reserve(capacity_ + (capacity_ / 2) + 2);
            write_pos = write_pos_.load(std::memory_order_relaxed);
            read_pos = read_pos_.load(std::memory_order_acquire);
        }
        data_[write_pos] = value;
        write_pos = (write_pos + 1) % capacity_;
        write_pos_.store(write_pos, std::memory_order_release);
        size_.store(size, std::memory_order_release);
    }

    inline T* pop() {
        resize_lock_.lock();
        uint32_t write_pos = write_pos_.load(std::memory_order_acquire);
        uint32_t read_pos = read_pos_.load(std::memory_order_relaxed);
        if (write_pos == read_pos) {
            resize_lock_.unlock();
            return nullptr;
        }
        T* ptr = data_ + read_pos;
        read_pos = (read_pos + 1) % capacity_;
        read_pos_.store(read_pos, std::memory_order_release);
        size_.fetch_sub(1, std::memory_order_relaxed);
        resize_lock_.unlock();
        return ptr;
    }

    inline uint32_t size() {
        return write_pos_.load(std::memory_order_relaxed) -
               read_pos_.load(std::memory_order_relaxed);
    }

    inline uint32_t num_items_written() { return write_pos_.load(std::memory_order_relaxed); }
    inline uint32_t num_items_read() { return read_pos_.load(std::memory_order_relaxed); }

    inline void reserve(size_t new_size) {
        if (new_size <= capacity_)
            return;

        T* new_data = (T*)std::malloc(new_size * sizeof(T));
        assert(new_data && "Cannot allocate memory");
        resize_lock_.lock();
        if (data_) {
            uint32_t read_pos = read_pos_.load(std::memory_order_relaxed);
            uint32_t size = size_.load(std::memory_order_relaxed);
            // Maybe we can split this into two memcpy?
            for (uint32_t i = 0; i < size; i++) {
                new_data[i] = data_[(read_pos + i) % capacity_];
            }
            write_pos_.store(size, std::memory_order_relaxed);
            read_pos_.store(0, std::memory_order_relaxed);
            std::free(data_);
        }
        capacity_ = new_size;
        data_ = new_data;
        resize_lock_.unlock();
    }
};

} // namespace wb

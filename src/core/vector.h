#pragma once

#include <initializer_list>
#include <iterator>
#include <memory>

#include "algorithm.h"
#include "common.h"
#include "types.h"

namespace wb {

template<typename T>
struct Vector {
  T* data_{};
  uint32_t size_{};
  uint32_t capacity_{};

  Vector() {
  }

  Vector(uint32_t size)
    requires std::is_default_constructible_v<T>
  {
    resize(size);
  }

  Vector(std::initializer_list<T> init_list) {
    reserve(init_list.size());
    for (auto& item : init_list) {
      push_back(item);
    }
  }

  Vector(Vector&& other) : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
  }

  ~Vector() {
    destroy_();
  }

  inline T& front() {
    assert(size_ != 0);
    return data_[0];
  }

  inline T& back() {
    assert(size_ != 0);
    return data_[size_ - 1];
  }

  inline const T& front() const {
    assert(size_ != 0);
    return data_[0];
  }

  inline const T& back() const {
    assert(size_ != 0);
    return data_[size_ - 1];
  }

  inline T& operator[](uint32_t i) {
    assert(i < size_);
    return data_[i];
  }

  inline const T& operator[](uint32_t i) const {
    assert(i < size_);
    return data_[i];
  }

  inline T& at(uint32_t i) {
    assert(i < size_);
    return data_[i];
  }

  inline const T& at(uint32_t i) const {
    assert(i < size_);
    return data_[i];
  }

  inline T* begin() noexcept {
    return data_;
  }

  inline T* end() noexcept {
    return data_ + size_;
  }

  inline const T* begin() const noexcept {
    return data_;
  }

  inline const T* end() const noexcept {
    return data_ + size_;
  }

  inline T* data() noexcept {
    return data_;
  }

  inline const T* data() const noexcept {
    return data_;
  }

  inline uint32_t size() const noexcept {
    return size_;
  }

  inline uint32_t capacity() const noexcept {
    return capacity_;
  }

  inline bool empty() const noexcept {
    return size_ == 0;
  }

  inline Vector<T>& operator=(const Vector<T>& other)
    requires std::copyable<T>
  {
    if (other.size_ == 0 || other.data_ == nullptr)
      return *this;
    uint32_t other_size = other.size();
    if constexpr (std::is_trivially_copyable_v<T>) {
      if (other_size != size_) {
        destroy_();
        data_ = (T*)std::malloc(other_size * sizeof(T));
        size_ = other_size;
        capacity_ = other_size;
      }
      std::memcpy(data_, other.data_, other_size * sizeof(T));
    } else {
      if (other_size != size_)
        resize(other_size);
      copy_n(data_, other.data_, other_size);
    }
    return *this;
  }

  inline Vector<T>& operator=(Vector<T>&& other) noexcept
    requires std::movable<T>
  {
    destroy_();
    data_ = other.data_;
    size_ = other.size_;
    capacity_ = other.capacity_;
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
    return *this;
  }

  template<typename It>
  inline T* append(It first, It last) {
    uint32_t old_size = size_;
    if constexpr (std::random_access_iterator<It>) {
      auto dist = std::distance(first, last);
      uint32_t new_size = size_ + dist;
      reserve_internal_(new_size);
      for (T* dst = data_ + size_; first != last; dst++, first++) {
        new (dst) T(*first);
      }
      size_ = new_size;
    } else {
      for (; first != last; first++) {
        push_back(*first);
      }
    }
    return data_ + old_size;
  }

  template<typename... Args>
  inline void emplace(T* it, Args&&... args) {
    assert(it >= data_);
    T* new_item = emplace_at_raw(it - data_);
    new (new_item) T(std::forward<Args>(args)...);
  }

  template<typename... Args>
  inline void emplace_at(uint32_t at, Args&&... args) {
    assert(at <= size_);
    T* new_item = emplace_at_raw(at);
    new (new_item) T(std::forward<Args>(args)...);
  }

  template<typename... Args>
  inline T& emplace_back(Args&&... args) {
    T* new_item = emplace_back_raw();
    new (new_item) T(std::forward<Args>(args)...);
    return *new_item;
  }

  inline T* emplace_at_raw(uint32_t at) {
    if (size_ == capacity_) {
      uint32_t new_capacity = grow_capacity_(capacity_);
      T* new_data = (T*)std::malloc(new_capacity * sizeof(T));
      assert(new_data != nullptr);

      if (data_) {
        if constexpr (std::is_move_constructible_v<T>) {
          move_initialize_n(new_data, data_, at);
          move_initialize_n(new_data + at + 1, data_ + at, size_ - at);
        } else if constexpr (std::is_copy_constructible_v<T>) {
          copy_initialize_n(new_data, data_, size_);
          copy_initialize_n(new_data + at + 1, data_ + at, size_ - at);
          destroy_n(data_, size_);
        }
        std::free(data_);
      }

      data_ = new_data;
      capacity_ = new_capacity;
      size_++;
      return data_ + at;
    }

    if constexpr (std::is_trivially_move_constructible_v<T> || std::is_trivially_copy_constructible_v<T>) {
      WB_BUILTIN_MEMMOVE(data_ + at + 1, data_ + at, (size_ - at) * sizeof(T));
    } else if constexpr (std::is_move_constructible_v<T>) {
      T* src = data_ + size_ - 1;
      T* dst = data_ + size_;
      uint32_t move_count = size_;
      while (move_count-- > at) {
        new (dst) T(std::move(*src));
        dst--;
        src--;
      }
    } else if constexpr (std::is_copy_constructible_v<T>) {
      T* src = data_ + size_ - 1;
      T* dst = data_ + size_;
      uint32_t move_count = size_;
      while (move_count-- > at) {
        new (dst) T(*src);
        src->~T();
        dst--;
        src--;
      }
    }

    size_++;
    return data_ + at;
  }

  inline T* emplace_back_raw() {
    if (size_ == capacity_) {
      reserve_internal_(grow_capacity_(capacity_));
    }
    T* ret = &data_[size_];
    size_++;
    return ret;
  }

  inline void push_front(const T& item)
    requires std::copy_constructible<T>
  {
    emplace_at(0, item);
  }

  inline void push_front(T&& item)
    requires std::move_constructible<T>
  {
    emplace_at(0, std::forward<T>(item));
  }

  inline void push_back(const T& item) {
    T* new_item = emplace_back_raw();
    new (new_item) T(item);
  }

  inline void push_back(T&& item) {
    T* new_item = emplace_back_raw();
    new (new_item) T(std::move(item));
  }

  inline void pop_back() {
    assert(size_ != 0);
    size_--;
    data_[size_].~T();
  }

  inline void erase_at(uint32_t at, uint32_t count = 1) {
    assert(at < size_);
    if (count != 0) {
      uint32_t src_offset = at + count;
      uint32_t move_count = size_ - src_offset;
      T* dst = data_ + at;
      T* src = data_ + src_offset;
      if constexpr (std::is_trivially_move_constructible_v<T> || std::is_trivially_copy_constructible_v<T>) {
        WB_BUILTIN_MEMMOVE(dst, src, move_count * sizeof(T));
      } else if constexpr (std::is_move_constructible_v<T>) {
        destroy_n(dst, count);
        for (uint32_t i = 0; i < move_count; i++) {
          new (&dst[i]) T(std::move(src[i]));
        }
      } else if constexpr (std::is_copy_constructible_v<T>) {
        destroy_n(dst, count);
        for (uint32_t i = 0; i < move_count; i++) {
          new (&dst[i]) T(src[i]);
          src[i].~T();
        }
      }
      size_ -= count;
    }
  }

  inline void erase(T* it) {
    assert(it >= data_);
    erase_at(it - data_);
  }

  inline void erase(T* begin, T* end) {
    assert(begin >= data_ && end < (data_ + size_));
    erase_at(begin - data_, end - begin);
  }

  inline void reserve(uint32_t n) {
    if (n > capacity_) {
      reserve_internal_(n);
    }
  }

  inline void resize(uint32_t size) {
    if (size > size_) {
      reserve(size);
      default_initialize_n(data_ + size_, size - size_);
    }
    if constexpr (!std::is_trivially_destructible_v<T>) {
      if (size < size_) {
        uint32_t num_destroy = size_ - size;
        destroy_n(data_ + size, num_destroy);
      }
    }
    size_ = size;
  }

  inline void resize(uint32_t size, const T& default_value) {
    if (size > size_) {
      reserve(size);
      fill_n(data_ + size_, default_value, size - size_);
      return;
    }
    if constexpr (!std::is_trivially_destructible_v<T>) {
      if (size < size_) {
        uint32_t num_destroy = size_ - size;
        destroy_n(data_ + size, num_destroy);
      }
    }
    size_ = size;
  }

  inline void resize_fast(uint32_t new_size)
    requires std::is_trivial_v<T>
  {
    if (new_size > capacity_)
      reserve_internal_(new_size);
    size_ = new_size;
  }

  inline void expand_size(uint32_t added_size) {
    resize(size_ + added_size);
  }

  inline void expand_capacity(uint32_t added_capacity) {
    reserve_internal_(size_ + added_capacity);
  }

  inline void clear() {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      for (uint32_t i = 0; i < size_; i++) {
        data_[i].~T();
      }
    }
    size_ = 0;
  }

  inline void reserve_internal_(uint32_t n) {
    T* new_data = (T*)std::malloc(n * sizeof(T));
    assert(new_data != nullptr);

    if (data_) {
      if constexpr (std::is_move_constructible_v<T>) {
        move_initialize_n(new_data, data_, size_);
      } else if constexpr (std::is_copy_constructible_v<T>) {
        copy_initialize_n(new_data, data_, size_);
        destroy_n(data_, size_);
      }
      std::free(data_);
    }

    data_ = new_data;
    capacity_ = n;
  }

  inline uint32_t grow_capacity_(uint32_t old_capacity) const {
    return old_capacity ? (old_capacity + old_capacity / 2) : 8;
  }

  inline void destroy_() {
    if (data_) {
      clear();
      std::free(data_);
      data_ = nullptr;
      capacity_ = 0;
    }
  }
};

}  // namespace wb
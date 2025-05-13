#pragma once

#include <cstring>

#include "core/common.h"

namespace wb {
struct BitSet {
  static constexpr uint32_t shift_amount = 5;
  static constexpr uint32_t subgroup_size = 32;
  static constexpr uint32_t mask = 0x1F;

  uint32_t* data_ = nullptr;
  uint32_t size_ = 0;
  uint32_t cap_ = 0;

  BitSet() {
  }
  
  BitSet(uint32_t size) {
    resize(size);
  }

  BitSet(const BitSet&) = delete;

  BitSet(BitSet&& other) noexcept : data_(other.data_), size_(other.size_), cap_(other.cap_) {
    other.data_ = {};
    other.size_ = {};
    other.cap_ = {};
  }

  ~BitSet() {
    if (data_)
      std::free(data_);
  }

  BitSet& operator=(BitSet&& other) noexcept {
    data_ = other.data_;
    size_ = other.size_;
    cap_ = other.cap_;
    other.data_ = nullptr;
    other.size_ = other.cap_ = 0u;
    return *this;
  }

  void resize(uint32_t n, bool fit = false) {
    if (!fit && n <= size_ || n == 0)
      return;
    uint32_t count = ((n - 1u) >> shift_amount) + 1u;
    if (count == cap_) {
      size_ = n;
      return;
    }
    uint32_t* new_data = (uint32_t*)std::calloc(count, sizeof(uint32_t));
    assert(new_data && "Cannot allocate memory");
    if (data_) {
      std::memcpy(new_data, data_, cap_ * sizeof(uint32_t));
      std::free(data_);
    }
    data_ = new_data;
    size_ = n;
    cap_ = count;
  }

  bool operator[](uint32_t n) const {
    assert(n < size_ && "Index out of bounds");
    return (data_[n >> shift_amount] >> (n & mask)) & 1;
  }

  inline bool get(uint32_t n) const {
    assert(n < size_ && "Index out of bounds");
    return (data_[n >> shift_amount] >> (n & mask)) & 1;
  }

  inline void set(uint32_t n) {
    assert(n < size_ && "Index out of bounds");
    data_[n >> shift_amount] |= 1 << (n & mask);
  }

  inline void unset(uint32_t n) {
    assert(n < size_ && "Index out of bounds");
    data_[n >> shift_amount] &= ~(1 << (n & mask));
  }

  void clear() {
    if (cap_ != 0)
      std::memset(data_, 0, cap_ * sizeof(uint32_t));
  }
};
}  // namespace wb
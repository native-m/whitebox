#pragma once

#include "core/common.h"

namespace wb {

template<typename T>
struct Span {
  T* data{};
  uint32_t size{};

  Span() {
  }

  Span(T* ptr, uint32_t size) : data(ptr), size(size) {
  }

  template<typename It>
  Span(It begin_it, It end_it) : data(&*begin_it),
                                 size(end_it - begin_it) {
  }

  Span(Span&& other) : data(other.data), size(other.size) {
    other.data = nullptr;
    other.size = 0;
  }

  template<typename It>
  inline void assign(It begin_it, It end_it) {
    assert(begin_it < end_it);
    auto length = end_it - begin_it;
    data = &*begin_it;
    size = (uint32_t)length;
  }

  inline T& front() noexcept {
    assert(size > 0);
    return data[0];
  }

  inline const T& front() const noexcept {
    assert(size > 0);
    return data[0];
  }

  inline T& back() noexcept {
    assert(size > 0);
    return data[size - 1];
  }

  inline const T& back() const noexcept {
    assert(size > 0);
    return data[size - 1];
  }

  inline T& at(uint32_t n) noexcept {
    assert(n < size && "Index out of bounds");
    return data[n];
  }

  inline const T& at(uint32_t n) const noexcept {
    assert(n < size && "Index out of bounds");
    return data[n];
  }

  T& operator[](uint32_t n) {
    assert(n < size && "Index out of bounds");
    return data[n];
  }

  const T& operator[](uint32_t n) const {
    assert(n < size && "Index out of bounds");
    return data[n];
  }

  T* begin() {
    return data;
  }

  const T* begin() const {
    return data;
  }

  T* end() {
    return data + size;
  }

  const T* end() const {
    return data + size;
  }
};

}  // namespace wb
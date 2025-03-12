#pragma once

#include <cstddef>

#include "common.h"
#include "io_types.h"

namespace wb {
struct ByteBuffer {
  std::byte* buffer = nullptr;
  size_t capacity_ = 0;
  size_t position_ = 0;
  size_t size_ = 0;
  bool managed_ = true;

  ByteBuffer() = default;
  ByteBuffer(size_t byte_reserved) {
    reserve(byte_reserved);
  }

  ByteBuffer(std::byte* bytes, size_t size, bool managed = true) : size_(size), managed_(managed) {
    if (managed) {
      reserve(size);
      std::memcpy(buffer, bytes, size);
    } else {
      capacity_ = size;
      buffer = bytes;
    }
  }

  ByteBuffer(const ByteBuffer&) = delete;

  ~ByteBuffer() {
    if (buffer && managed_)
      std::free(buffer);
  }

  inline void reset() {
    position_ = 0;
    size_ = 0;
  }

  inline bool seek(int64_t offset, IOSeekMode mode) {
    switch (mode) {
      case IOSeekMode::Begin: position_ = offset; break;
      case IOSeekMode::Relative: position_ = (size_t)((int64_t)position_ + offset); break;
      case IOSeekMode::End: position_ = (size_t)((int64_t)size_ + offset); break;
      default: WB_UNREACHABLE();
    }
    return true;
  }

  inline void reserve(size_t n) {
    if (n > capacity_) {
      std::byte* new_buffer = (std::byte*)std::malloc(n);
      assert(new_buffer && "Cannot reserve buffer");
      if (buffer) {
        std::memcpy(new_buffer, buffer, position_);
        if (managed_)
          std::free(buffer);
        managed_ = true;
      }
      buffer = new_buffer;
      capacity_ = n;
    }
  }

  inline uint32_t read(void* src, size_t read_size) {
    size_t next_read_pos = position_ + read_size;
    if (next_read_pos > size_)
      return 0;
    std::memcpy(src, buffer + position_, read_size);
    position_ = next_read_pos;
    return read_size;
  }

  inline uint32_t write(const void* src, size_t write_size) {
    size_t next_write_pos = position_ + write_size;
    if (next_write_pos > capacity_)
      reserve(next_write_pos + (256 - (next_write_pos & 255)));
    std::byte* ptr = buffer + position_;
    std::memcpy(ptr, src, write_size);
    position_ = next_write_pos;
    size_ = next_write_pos > size_ ? next_write_pos : size_;
    return (uint32_t)write_size;
  }

  inline size_t position() const {
    return position_;
  }
  inline std::byte* data() {
    return buffer;
  }
  inline const std::byte* data() const {
    return buffer;
  }
};
}  // namespace wb
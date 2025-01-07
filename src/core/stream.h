#pragma once

#include "common.h"
#include "types.h"
#include "io_types.h"
#include <utility>

namespace wb {
template <typename T>
concept IOReader = requires(T v, void* ptr, size_t size) {
    { v.read(ptr, size) } -> std::same_as<uint32_t>;
};

template <typename T>
concept IOWriter = requires(T v, const void* ptr, size_t size) {
    { v.write(ptr, size) } -> std::same_as<uint32_t>;
};

template <typename T>
concept IOStream = IOReader<T> && IOWriter<T> && requires(T v, int64_t offset, IOSeekMode seek_mode) {
    { v.seek(offset, seek_mode) } -> std::same_as<bool>;
    { v.position() } -> std::same_as<uint64_t>;
};

template <typename T, typename R>
concept IODeserializable = IOReader<R> && requires(T v, R* reader) {
    { v.read_from_stream(reader) } -> std::same_as<uint32_t>;
};

template <typename T, typename W>
concept IOSerializable = IOWriter<W> && requires(T v, W* writer) {
    { v.write_to_stream(writer) } -> std::same_as<uint32_t>;
};

template <IOReader R>
inline uint32_t io_read_byte(R& r, std::byte* byte) {
    return r.read(byte, 1);
}

template <IOReader R>
inline uint32_t io_read_bytes(R& r, std::byte* data, size_t size) {
    return r.read(data, size);
}

template <IOReader R, typename T>
    requires NumericalType<T> || DynamicArrayContainer<T>
inline uint32_t io_read(R& r, T* value) {
    if constexpr (NumericalType<T>) {
        return r.read(value, sizeof(T));
    } else if constexpr (ContinuousArrayContainer<T>) {
        uint32_t size;
        uint32_t num_size_read = r.read(&size, sizeof(uint32_t));
        if (size == 0)
            return num_size_read;
        value->resize(size);
        uint32_t byte_size = size * sizeof(typename T::value_type);
        uint32_t num_data_read = r.read(value->data(), byte_size);
        if (num_data_read < byte_size)
            return 0;
        return num_size_read + num_data_read;
    }
    return 0;
}

template <IOWriter W>
inline uint32_t io_write_byte(W& w, std::byte value) {
    return w.write(&value, 1);
}

template <IOWriter W>
inline uint32_t io_write_bytes(W& w, std::byte* data, size_t size) {
    return w.write(data, size);
}

template <IOWriter W, typename T>
    requires NumericalType<T> || ContinuousArrayContainer<T>
inline uint32_t io_write(W& w, const T& value) {
    if constexpr (NumericalType<T>) {
        return w.write(&value, sizeof(T));
    } else if constexpr (ContinuousArrayContainer<T>) {
        uint32_t size = value.size();
        uint32_t num_size_written = w.write(&size, sizeof(uint32_t));
        if (size == 0)
            return num_size_written;
        uint32_t byte_size = size * sizeof(typename T::value_type);
        uint32_t num_data_written = w.write(value.data(), byte_size);
        if (num_data_written < byte_size)
            return 0;
        return num_size_written + num_data_written;
    }
    return 0;
}
}; // namespace wb
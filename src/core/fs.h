#pragma once

#include "common.h"
#include "types.h"
#include <filesystem>
#include <optional>

namespace wb {
struct File;

template <typename T>
concept FileSerializable = requires(T v, File* file) {
    { v.write_to_file(file) } -> std::same_as<uint32_t>;
};

template <typename T>
concept FileDeserializable = requires(T v, File* file) {
    { v.read_from_file(file) } -> std::same_as<uint32_t>;
};

struct File {
    enum {
        // File open flags
        Read = 1,
        Write = 2,
        Truncate = 4,

        // Seek modes
        SeekBegin = 0,
        SeekRelative = 1,
        SeekEnd = 2,

        // Errors
        ErrEndOfFile,
    };

    void* handle_;
    bool open_ {};

    File();
    ~File();

    bool open(const std::filesystem::path& path, uint32_t flags);
    bool seek(int64_t offset, uint32_t mode);
    uint64_t position() const;
    uint32_t read(void* dest, size_t size);
    uint32_t write(const void* src, size_t size);
    void close();

    inline uint32_t read_i32(int32_t* value) { return read(value, sizeof(int32_t)); }
    inline uint32_t read_u32(uint32_t* value) { return read(value, sizeof(uint32_t)); }
    inline uint32_t read_f32(float* value) { return read(value, sizeof(float)); }
    inline uint32_t read_i64(int32_t* value) { return read(value, sizeof(int64_t)); }
    inline uint32_t read_u64(uint32_t* value) { return read(value, sizeof(uint64_t)); }
    inline uint32_t read_f64(double* value) { return read(value, sizeof(double)); }
    inline uint32_t read_string(char* str, size_t size) { return read(str, size); }

    inline uint32_t write_i32(int32_t value) { return write(&value, sizeof(int32_t)); }
    inline uint32_t write_u32(uint32_t value) { return write(&value, sizeof(uint32_t)); }
    inline uint32_t write_f32(float value) { return write(&value, sizeof(float)); }
    inline uint32_t write_i64(int32_t value) { return write(&value, sizeof(int64_t)); }
    inline uint32_t write_u64(uint32_t value) { return write(&value, sizeof(uint64_t)); }
    inline uint32_t write_f64(double value) { return write(&value, sizeof(double)); }
    inline uint32_t write_string(const char* str, size_t size) { return write(str, size); }
    inline uint32_t write_string(const std::string& str) { return write(str.data(), str.size()); }

    template <DynamicArrayContainer T>
    uint32_t read_array(T& out) {
        uint32_t size;
        uint32_t num_size_read = read(&size, sizeof(uint32_t));
        if (size == 0)
            return num_size_read;
        out.resize(size);
        uint32_t byte_size = size * sizeof(typename T::value_type);
        uint32_t num_data_read = read(out.data(), byte_size);
        if (num_data_read < byte_size)
            return 0;
        return num_size_read + num_data_read;
    }

    template <ContinuousArrayContainer T>
    inline uint32_t write_array(const T& out) {
        uint32_t size = out.size();
        uint32_t num_size_written = write(&size, sizeof(uint32_t));
        if (size == 0)
            return num_size_written;
        uint32_t byte_size = size * sizeof(typename T::value_type);
        uint32_t num_data_written = write(out.data(), byte_size);
        if (num_data_written < byte_size)
            return 0;
        return num_size_written + num_data_written;
    }

    inline bool is_open() const { return open_; }
};

consteval uint32_t fourcc(const char ch[5]) {
    // TODO: Big endian support
    return ch[0] | (ch[1] << 8) | (ch[2] << 16) | (ch[3] << 24);
}

std::filesystem::path to_system_preferred_path(const std::filesystem::path& path);
std::filesystem::path remove_filename_from_path(const std::filesystem::path& path);
void explore_folder(const std::filesystem::path& path);
void locate_file(const std::filesystem::path& path);
std::optional<std::filesystem::path> find_file_recursive(const std::filesystem::path& dir,
                                                         const std::filesystem::path& filename);

} // namespace wb
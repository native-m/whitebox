#pragma once

#include "common.h"
#include <concepts>
#include <filesystem>

namespace wb {
struct File;

template <typename T>
concept FileSerializable = requires(T v, File* file) {
    { v.write_to_file(file) } -> std::same_as<bool>;
};

template <typename T>
concept FileDeserializable = requires(T v, File* file) {
    { v.read_from_file(file) } -> std::same_as<bool>;
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
    };

    void* handle_;
    bool open_ {};

    File();
    ~File();

    bool open(const std::filesystem::path& path, uint32_t flags);
    bool seek(int64_t offset, uint32_t mode);
    uint32_t read(void* dest, uint32_t size);
    uint32_t write(const void* src, uint32_t size);
    void close();

    inline uint32_t read_i32(int32_t* value) { return read(value, sizeof(int32_t)); }
    inline uint32_t read_u32(uint32_t* value) { return read(value, sizeof(uint32_t)); }
    inline uint32_t read_f32(float* value) { return read(value, sizeof(float)); }
    inline uint32_t read_i64(int32_t* value) { return read(value, sizeof(int64_t)); }
    inline uint32_t read_u64(uint32_t* value) { return read(value, sizeof(uint64_t)); }
    inline uint32_t read_f64(double* value) { return read(value, sizeof(double)); }
    inline bool read_struct(FileDeserializable auto& data) { return data.read_from_file(this); }

    inline uint32_t write_i32(int32_t value) { return write(&value, sizeof(int32_t)); }
    inline uint32_t write_u32(uint32_t value) { return write(&value, sizeof(uint32_t)); }
    inline uint32_t write_f32(float value) { return write(&value, sizeof(float)); }
    inline uint32_t write_i64(int32_t value) { return write(&value, sizeof(int64_t)); }
    inline uint32_t write_u64(uint32_t value) { return write(&value, sizeof(uint64_t)); }
    inline uint32_t write_f64(double value) { return write(&value, sizeof(double)); }
    inline bool write_struct(FileSerializable auto& data) { return data.write_to_file(this); }

    inline bool is_open() const { return open_; }
};

std::filesystem::path to_system_preferred_path(const std::filesystem::path& path);
void explore_folder(const std::filesystem::path& path);
void locate_file(const std::filesystem::path& path);
} // namespace wb
#pragma once

#include "core/audio_buffer.h"
#include "core/debug.h"
#include "core/memory.h"
#include "core/vector.h"
#include "track_input.h"
#include <atomic>
#include <vector>
#include <memory>
#include <semaphore>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace wb {

struct AudioRecordBuffer {
    void* channel_buffer_ {};

    ~AudioRecordBuffer() {
        if (channel_buffer_)
            std::free(channel_buffer_);
    }

    void init(uint32_t channel_count, uint32_t buffer_size, AudioFormat format) {
        uint32_t format_size = get_audio_format_size(format);
        uint32_t buffer_size_bytes = format_size * buffer_size * channel_count;
        channel_buffer_ = std::malloc(buffer_size_bytes);
        assert(channel_buffer_ != nullptr && "Cannot allocate recording buffer");
    }

    template <typename T>
    inline const T* get_read_pointer(uint32_t channel, uint32_t buffer_size) const {
        return static_cast<const T*>(channel_buffer_) + (buffer_size * channel);
    }

    template <typename T>
    inline T* get_write_pointer(uint32_t channel, uint32_t buffer_size) {
        return static_cast<T*>(channel_buffer_) + (buffer_size * channel);
    }
};

struct AudioRecordQueue {
    struct alignas(64) SharedData {
        std::atomic_uint32_t pos;
        std::atomic_uint32_t should_signal;
    };

    Vector<AudioRecordBuffer> recording_buffers_;
    uint32_t buffer_capacity_ = 0;
    uint32_t sample_size_ = 0;
    SharedData writer_;
    SharedData reader_;
    alignas(64) std::atomic_uint32_t size_;

    uint32_t current_write_pos = 0;
    uint32_t current_write_size = 0;
    uint32_t next_write_pos = 0;
    uint32_t next_write_size = 0;
    uint32_t current_read_pos = 0;
    uint32_t current_read_size = 0;
    uint32_t next_read_pos = 0;
    uint32_t next_read_size = 0;

    ~AudioRecordQueue() { recording_buffers_.clear(); }

    void start(AudioFormat format, uint32_t buffer_size, std::vector<TrackInputGroup>& input_mapping);
    void stop();

    void begin_write(uint32_t write_size);
    void end_write();

    bool begin_read(uint32_t read_size);
    void end_read();

    template <std::floating_point T>
    void write(uint32_t buffer_id, uint32_t start_channel, uint32_t num_channels, const AudioBuffer<T>& buffer) {
        AudioRecordBuffer& record_buffer = recording_buffers_[buffer_id];
        if (current_write_pos <= next_write_pos) {
            for (uint32_t i = 0; i < num_channels; i++) {
                const T* src_channel_buffer = buffer.get_read_pointer(i + start_channel);
                T* dst_channel_buffer = record_buffer.get_write_pointer<T>(i, buffer_capacity_);
                std::memcpy(dst_channel_buffer + current_write_pos, src_channel_buffer, current_write_size * sample_size_);
            }
            //Log::debug("Write: {}", current_write_pos);
        } else {
            for (uint32_t i = 0; i < num_channels; i++) {
                const T* src_channel_buffer = buffer.get_read_pointer(i + start_channel);
                T* dst_channel_buffer = record_buffer.get_write_pointer<T>(i, buffer_capacity_);
                uint32_t split_size = buffer_capacity_ - current_write_pos;
                std::memcpy(dst_channel_buffer + current_write_pos, src_channel_buffer, split_size * sample_size_);
                if (next_write_pos != 0)
                    std::memcpy(dst_channel_buffer, src_channel_buffer + split_size, next_write_pos * sample_size_);
            }
        }
    }

    template <std::floating_point T>
    void read(uint32_t buffer_id, T* const* dst_buffer, size_t dst_offset, uint32_t start_channel, uint32_t num_channels) {
        AudioRecordBuffer& record_buffer = recording_buffers_[buffer_id];
        if (current_read_pos <= next_read_pos) {
            for (uint32_t i = 0; i < num_channels; i++) {
                T* dst_channel_buffer = dst_buffer[i] + dst_offset;
                const T* src_channel_buffer =
                    record_buffer.get_read_pointer<T>(i + start_channel, buffer_capacity_);
                std::memcpy(dst_channel_buffer, src_channel_buffer + current_read_pos, current_read_size * sample_size_);
            }
        } else {
            for (uint32_t i = 0; i < num_channels; i++) {
                T* dst_channel_buffer = dst_buffer[i] + dst_offset;
                const T* src_channel_buffer =
                    record_buffer.get_read_pointer<T>(i + start_channel, buffer_capacity_);
                uint32_t split_size = buffer_capacity_ - current_read_pos;
                std::memcpy(dst_channel_buffer, src_channel_buffer + current_read_pos, split_size * sample_size_);
                if (next_read_pos != 0)
                    std::memcpy(dst_channel_buffer + split_size, src_channel_buffer, next_read_pos * sample_size_);
            }
        }
    }

    uint32_t size() const { return size_.load(); }

    /*void set_size(uint32_t new_channel_count, uint32_t new_buffer_capacity, AudioFormat format) {
        if (new_channel_count == channel_count_ && new_buffer_capacity == buffer_capacity_)
            return;
        if (new_channel_count > channel_capacity_) {
            void** new_channel_buffers = (void**)std::malloc(sizeof(void*) * new_channel_count);
            channel_buffers_ = new_channel_buffers;
            channel_capacity_ = new_channel_count;
        }
        uint32_t format_size = get_audio_format_size(format);
        uint32_t buffer_byte_size = format_size * new_buffer_capacity * new_channel_count;
        for (uint32_t i = 0; i < new_channel_count; i++) {
            void* buffer = allocate_aligned(buffer_byte_size, 32);
            assert(buffer != nullptr && "Cannot allocate");
            channel_buffers_[i] = buffer;
        }
        buffer_capacity_ = new_buffer_capacity;
        channel_count_ = new_channel_count;
    }

    template <std::floating_point T>
    void write(const AudioBuffer<T>& buffer) {
        assert(buffer.n_samples < buffer_capacity_ && buffer.n_channels == 0);
        uint32_t write_pos = write_pos_.load(std::memory_order_relaxed);
        uint32_t read_pos = read_pos_.load(std::memory_order_acquire);
        uint32_t new_write_pos = (write_pos + buffer.n_samples) % buffer_capacity_;

        if (write_pos <= new_write_pos) {
            for (uint32_t i = 0; i < buffer.n_channels; i++) {
                T* dst_channel_buffer = (T*)channel_buffers_[i] + write_pos;
                T* src_channel_buffer = buffer.get_read_pointer(i);
                std::memcpy(dst_channel_buffer, src_channel_buffer, buffer.n_samples);
            }
        } else {
            for (uint32_t i = 0; i < buffer.n_channels; i++) {
                T* dst_channel_buffer = (T*)channel_buffers_[i];
                T* src_channel_buffer = buffer.get_read_pointer(i);
                uint32_t split_size = buffer_capacity_ - write_pos;
                std::memcpy(dst_channel_buffer + write_pos, src_channel_buffer, split_size);
                std::memcpy(dst_channel_buffer, src_channel_buffer + split_size, new_write_pos);
            }
        }

        write_pos_.store(new_write_pos, std::memory_order_release);
        size_.fetch_add(buffer.n_samples, std::memory_order_release);
    }

    template <std::floating_point T>
    bool read(T* const* dst_buffer, uint32_t channel, uint32_t read_count) {
        uint32_t read_pos = read_pos_.load(std::memory_order_relaxed);
        uint32_t write_pos = write_pos_.load(std::memory_order_acquire);
        uint32_t new_read_pos = (read_pos + read_count) % buffer_capacity_;

        if (read_pos <= new_read_pos) {
            for (uint32_t i = 0; i < channel; i++) {
                T* dst_channel_buffer = dst_buffer[i] + read_pos;
                T* src_channel_buffer = (T*)channel_buffers_[i];
                std::memcpy(dst_channel_buffer, src_channel_buffer, read_count);
            }
        } else {
            for (uint32_t i = 0; i < channel; i++) {
                T* dst_channel_buffer = dst_buffer[i] + read_pos;
                T* src_channel_buffer = (T*)channel_buffers_[i];
                uint32_t split_size = buffer_capacity_ - read_pos;
                std::memcpy(dst_channel_buffer + read_pos, src_channel_buffer, split_size);
                std::memcpy(dst_channel_buffer, src_channel_buffer + split_size, new_read_pos);
            }
        }

        read_pos_.store(new_read_pos, std::memory_order_release);
        size_.fetch_sub(read_count, std::memory_order_release);
    }*/
};

} // namespace wb
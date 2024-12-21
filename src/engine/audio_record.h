#pragma once

#include "core/audio_buffer.h"
#include "core/memory.h"
#include "core/vector.h"
#include <atomic>
#include <memory>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace wb {

using AudioInputMapping = std::unordered_map<uint32_t, std::unordered_set<uint32_t>>;

struct AudioRecordBuffer {
    void* channel_buffer_ {};
    uint32_t channel_count_ {};

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
        return static_cast<const T*>(channel_buffer_) + buffer_size * channel;
    }

    template <typename T>
    inline T* get_write_pointer(uint32_t channel, uint32_t buffer_size) {
        return static_cast<T*>(channel_buffer_) + buffer_size * channel;
    }
};

struct AudioRecordFile
{

};

struct AudioRecordQueue {
    uint32_t buffer_capacity_ = 0;
    std::atomic_uint32_t write_pos_;
    std::atomic_uint32_t read_pos_;
    std::atomic_uint32_t size_;
    Vector<AudioRecordBuffer> recording_buffers_;

    ~AudioRecordQueue() { recording_buffers_.clear(); }

    void start(AudioFormat format, uint32_t buffer_size, const AudioInputMapping& input_mapping);
    void stop();

    template <std::floating_point T>
    void write(uint32_t buffer_id, const AudioBuffer<T>& buffer)
    {

    }

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
#include "audio_record.h"
#include "track_input.h"

namespace wb {
void AudioRecordQueue::start(AudioFormat format, uint32_t buffer_size, const AudioInputMapping& input_mapping) {
    uint32_t idx = 0;
    buffer_capacity_ = buffer_size;
    recording_buffers_.resize(input_mapping.size());
    for (auto& [type, track] : input_mapping) {
        auto input = TrackInput::from_packed_u32(type);
        uint32_t channel_count = (input.type == TrackInputType::ExternalMono) ? 1 : 2;
        recording_buffers_[idx].init(channel_count, buffer_size, format);
        idx++;
    }
}

void AudioRecordQueue::stop() {
    recording_buffers_.clear();
}

void AudioRecordQueue::begin_write(uint32_t write_size) {
    for (;;) {
        uint32_t write_pos = writer_.pos.load(std::memory_order_relaxed);
        uint32_t read_pos = reader_.pos.load(std::memory_order_acquire);
        uint32_t size = size_.load(std::memory_order_acquire);
        if (size >= write_size) {
            current_write_pos = write_pos;
            current_write_size = write_size;
            next_write_pos = (write_pos + write_size) % buffer_capacity_;
            next_write_size = size + write_size;
            break;
        }
        reader_.should_signal.store(1, std::memory_order_release);
        size_.wait(size, std::memory_order_acquire);
    }
}

void AudioRecordQueue::end_write() {
    writer_.pos.store(next_write_pos, std::memory_order_release);
    size_.store(next_write_size, std::memory_order_release);
    //if (writer_.should_signal.exchange(0, std::memory_order_release))
    //    size_.notify_one(); // Notify reader thread
}

bool AudioRecordQueue::begin_read(uint32_t read_size) {
    uint32_t read_pos = reader_.pos.load(std::memory_order_relaxed);
    uint32_t write_pos = writer_.pos.load(std::memory_order_acquire);
    uint32_t size = size_.load(std::memory_order_acquire);

    if (size >= read_size) {
        current_read_pos = read_pos;
        current_read_size = read_size;
        next_read_pos = (read_pos + read_size) % buffer_capacity_;
        next_read_size = size - read_size;
        return true;
    }

    /*for (;;) {
        writer_.should_signal.store(1, std::memory_order_release);
        size_.wait(size, std::memory_order_acquire);
    }*/

    return false;
}

void AudioRecordQueue::end_read() {
    reader_.pos.store(next_read_pos, std::memory_order_release);
    size_.store(next_read_size, std::memory_order_release);
    if (reader_.should_signal.exchange(0, std::memory_order_release))
        size_.notify_one(); // Notify writer thread
}

} // namespace wb
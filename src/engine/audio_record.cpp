#include "audio_record.h"
#include "track_input.h"

namespace wb {
void AudioRecordQueue::start(AudioFormat format, uint32_t buffer_size, const AudioInputMapping& input_mapping) {
    uint32_t idx = 0;
    buffer_capacity_ = buffer_size;
    recording_buffers_.resize(input_mapping.size());
    for (auto& [mode, track] : input_mapping) {
        auto input = TrackInput::from_packed_u32(mode);
        uint32_t channel_count = (input.mode == TrackInputMode::ExternalMono) ? 1 : 2;
        recording_buffers_[idx].init(channel_count, buffer_size, format);
        idx++;
    }
}

void AudioRecordQueue::stop() {
    recording_buffers_.clear();
}
} // namespace wb
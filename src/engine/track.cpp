#include "track.h"
#include "../core/debug.h"
#include "../core/midi.h"
#include <algorithm>

namespace wb
{
    Track::Track(TrackType type, const std::string& name) :
        type(type),
        name(name),
        color(0.3f, 0.3f, 0.3f, 1.0f)
    {
        switch (type) {
            case TrackType::Audio:
                clip_allocator.emplace(std::in_place_type<Pool<AudioClip>>);
                break;
            case TrackType::Midi:
                clip_allocator.emplace(std::in_place_type<Pool<MIDIClip>>);
                break;
        }

        // Helper nodes
        head_node.name = "Head";
        tail_node.name = "Tail";
        tail_node.push_front(&head_node);

        dbg_message.reserve(128);
    }

    Track::~Track()
    {
        Clip* current = head_node.next;
        while (current != &tail_node) {
            current->~Clip();
            current = current->next;
        }
    }

    AudioClip* Track::add_audio_clip(double min_time, double max_time, AudioClip* nearby_clip)
    {
        if (type != TrackType::Audio)
            return nullptr;

        auto clip = new(std::get<Pool<AudioClip>>(*clip_allocator).allocate()) AudioClip(min_time, max_time);
        clip->color = color;

        if (head_node.next == &tail_node) {
            head_node.push_back(clip);
            return clip;
        }

        AudioClip* adjacent_clip = (AudioClip*)seek_adjacent_clip(min_time);
        if (min_time < adjacent_clip->min_time)
            adjacent_clip->push_front(clip);
        else
            adjacent_clip->push_back(clip);

#ifndef NDEBUG
        //log_clip_ordering_();
#endif
        return clip;
    }

    void Track::move_clip(Clip* clip, double relative_pos)
    {
        WB_ASSERT(clip);
        if (relative_pos == 0.0) return;
        double new_pos = std::max(clip->min_time + relative_pos, 0.0);

        if (new_pos > clip->min_time) {
            Clip* adjacent_clip = seek_forward(new_pos, clip);
            if (adjacent_clip != clip) {
                clip->detach();
                adjacent_clip->push_front(clip);
            }
        }
        else {
            Clip* adjacent_clip = seek_backward(new_pos, clip);
            if (adjacent_clip != clip) {
                clip->detach();
                adjacent_clip->push_back(clip);
            }
        }

        clip->max_time = new_pos + (clip->max_time - clip->min_time);
        clip->min_time = new_pos;

#ifndef NDEBUG
        log_clip_ordering_();
#endif
    }

    void Track::resize_clip(Clip* clip, double relative_pos, bool right_side)
    {
        if (right_side) {
            double new_pos = std::max(clip->max_time + relative_pos, 0.0);
            if (new_pos <= clip->min_time)
                new_pos = clip->min_time + 1.0;
            clip->max_time = new_pos;
            return;
        }

        double new_pos = std::max(clip->min_time + relative_pos, 0.0);
        if (new_pos >= clip->max_time)
            new_pos = clip->max_time + 1.0;

        if (new_pos > clip->min_time) {
            Clip* adjacent_clip = seek_forward(new_pos, clip);
            if (adjacent_clip != clip) {
                clip->detach();
                adjacent_clip->push_front(clip);
            }
        }
        else {
            Clip* adjacent_clip = seek_backward(new_pos, clip);
            if (adjacent_clip != clip) {
                clip->detach();
                adjacent_clip->push_back(clip);
            }
        }

        clip->min_time = new_pos;

#ifndef NDEBUG
        log_clip_ordering_();
#endif
    }

    void Track::delete_clip(Clip* clip)
    {
        deleted_clips.insert(clip);
    }

    Clip* Track::find_clip(double time, Clip* nearby_clip)
    {
        Clip* first_clip = (Clip*)head_node.next;
        Clip* last_clip = (Clip*)tail_node.prev;

        double half_distance_time = last_clip->min_time * 0.5f;
        if (time > half_distance_time) {
            // Seek backward from the last clip
            Clip* current = last_clip;
            while (current && current->min_time > time)
                current = current->prev;
            return current;
        }

        // Seek forward from the first clip
        Clip* current = first_clip;
        while (current && current->min_time < time)
            current = current->next;

        return current;
    }

    Clip* Track::seek_adjacent_clip(double time, Clip* hint)
    {
        Clip* first_clip = head_node.next;
        Clip* last_clip = tail_node.prev;

        if (first_clip == last_clip)
            return first_clip;

        double half_distance_time = last_clip->min_time * 0.5f;
        if (time > half_distance_time)
            return seek_backward(time, last_clip);

        return seek_forward(time, first_clip);
    }

    Clip* Track::seek_backward(double time, Clip* clip)
    {
        while (clip != head_node.next && clip->min_time > time)
            clip = clip->prev;
        return clip;
    }

    Clip* Track::seek_forward(double time, Clip* clip)
    {
        while (clip != &tail_node && clip->min_time < time)
            clip = clip->next;
        return clip;
    }

    void Track::flush_deleted_clips()
    {
        Clip* old_currently_playing_clip = currently_playing_clip.load(std::memory_order_relaxed);
        Clip* old_next_clip = next_clip.load(std::memory_order_relaxed);

        for (auto clip : deleted_clips) {
            if (old_currently_playing_clip == clip)
                currently_playing_clip.store(nullptr, std::memory_order_relaxed);
            if (old_next_clip == clip)
                next_clip.store(old_next_clip->next, std::memory_order_relaxed);
            clip->detach();
            clip->~Clip();
            std::get<Pool<AudioClip>>(*clip_allocator).free(clip);
        }

        deleted_clips.clear();
        Log::info("Clips flushed");
    }

    void Track::update_play_state(double position)
    {
        // Invalidate currently playing clip
        currently_playing_clip.store(nullptr, std::memory_order_release);
        last_position = position;

        Clip* new_next_clip = seek_adjacent_clip(position);
        if (new_next_clip != &tail_node && new_next_clip != &head_node) {
            last_position_clip = new_next_clip;
            next_clip.store(new_next_clip, std::memory_order_release);

            Log::info("Node: {:x} {} {} {}",
                      reinterpret_cast<uintptr_t>(new_next_clip),
                      new_next_clip->name,
                      new_next_clip->min_time,
                      new_next_clip->max_time);
        }

        dbg_message.clear();
    }

    void Track::stop()
    {
        current_message = {};
        samples_processed = 0;
    }

    void Track::process_message(double offset, double current_position, double beat_duration, double sample_rate)
    {
        Clip* old_currently_playing_clip = currently_playing_clip.load(std::memory_order_acquire);
        Clip* old_next_clip = next_clip.load(std::memory_order_acquire);
        double current_sec = beat_to_seconds(current_position - offset, beat_duration);

        if (old_currently_playing_clip && current_position >= old_currently_playing_clip->max_time) {
            uint64_t sample_pos = (uint64_t)(current_sec * sample_rate);
            currently_playing_clip.store(nullptr, std::memory_order_release);
            message_queue.push(
                TrackMessage{
                    .sample_position = sample_pos,
                    .audio = AudioMessage::end()
                });
        }

        if (old_next_clip && old_next_clip != &tail_node && current_position >= old_next_clip->min_time) {
            double min_time_sec = beat_to_seconds(old_next_clip->min_time - offset, beat_duration);
            uint32_t start_sample = (uint32_t)((current_sec - min_time_sec) * sample_rate);
            auto msg = TrackMessage{
                .sample_position = (uint64_t)(current_sec * sample_rate),
                .audio = AudioMessage::start((AudioClip*)old_next_clip, start_sample)
            };
            currently_playing_clip.store(next_clip, std::memory_order_release); // Set current clip
            next_clip.store(old_next_clip->next, std::memory_order_release);
            message_queue.push(msg);
            dbg_message.push_back(msg);
            Log::info("Trigger: {} {} {}", current_position, old_next_clip->min_time, (uint64_t)(current_sec * sample_rate));
        }
    }

    void Track::process(AudioBuffer<float>& output_buffer, double sample_rate, double tick_duration, bool is_playing)
    {
        if (is_playing) {
            if (message_queue.empty()) {
                // Continue process the whole block
                switch (current_message.audio.status) {
                    case AudioStatus::Play:
                    {
                        play_sample(output_buffer, current_message, samples_processed);
                        break;
                    }
                    case AudioStatus::Stop:
                        samples_processed = 0;
                        break;
                }
            }
            else {
                while (message_queue.size() > 0) {
                    last_message = current_message;
                    current_message = *message_queue.pop();
                    switch (current_message.audio.status) {
                        case AudioStatus::Play:
                        {
                            samples_processed = current_message.audio.start_sample;
                            play_sample(output_buffer, current_message, samples_processed);
                            break;
                        }
                        case AudioStatus::Stop:
                        {
                            // Before stopping, check if we still need to continue sample playback.
                            if (last_message.audio.status == AudioStatus::Play &&
                                last_message.sample_position < current_message.sample_position)
                                play_sample(output_buffer, last_message, samples_processed);
                            samples_processed = 0;
                            break;
                        }
                    }
                }
            }
        }
    }

    void Track::play_sample(AudioBuffer<float>& output_buffer, TrackMessage& msg, uint32_t offset)
    {
        Sample* sample = msg.audio.clip->get_sample_instance();
        uint32_t position_at_buffer = (uint32_t)(msg.sample_position % (uint64_t)output_buffer.n_samples);
        
        // Make sure we do not pass the sample_length
        uint32_t samples_produced = std::min(output_buffer.n_samples - position_at_buffer,
                                             (uint32_t)sample->sample_count - offset);

        if (offset < sample->sample_count) {
            for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
                float* output = output_buffer.get_write_pointer(i);
                float* sample_data = (float*)sample->sample_data_[i];
                for (uint32_t j = 0; j < samples_produced; j++) {
                    output[j + position_at_buffer] += sample_data[offset + j];
                }
            }
        }

        // Ascend current message sample position and the number of processed samples
        msg.sample_position += samples_produced;
        samples_processed += samples_produced;
    }

    void Track::log_clip_ordering_()
    {
        Clip* current = (Clip*)head_node.next;
        Log::info("-----");
        while (current != &tail_node) {
            Log::info("{}", current->name);
            current = (Clip*)current->next;
        }
    }
}
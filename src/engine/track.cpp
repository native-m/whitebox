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

        head_node.push_back(&tail_node);
    }

    Track::~Track()
    {
        ClipNode* current = head_node.next;
        while (current != &tail_node) {
            current->~ClipNode();
            current = current->next;
        }
    }

    AudioClip* Track::add_audio_clip(double min_time, double max_time, AudioClip* nearby_clip)
    {
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

    Clip* Track::find_clip(double time, Clip* nearby_clip)
    {
        Clip* first_clip = (Clip*)head_node.next;
        Clip* last_clip = (Clip*)tail_node.prev;

        double half_distance_time = last_clip->min_time * 0.5f;
        if (time > half_distance_time) {
            // Seek backward from the last clip
            Clip* current = last_clip;
            while (current && current->min_time > time)
                current = (Clip*)current->prev;
            return current;
        }

        // Seek forward from the first clip
        Clip* current = first_clip;
        while (current && current->min_time < time)
            current = (Clip*)current->next;

        return current;
    }

    Clip* Track::seek_adjacent_clip(double time, Clip* hint)
    {
        Clip* first_clip = (Clip*)head_node.next;
        Clip* last_clip = (Clip*)tail_node.prev;

        if (first_clip == last_clip)
            return first_clip;

        double half_distance_time = last_clip->min_time * 0.5f;
        if (time > half_distance_time)
            return seek_backward(time, last_clip);

        return seek_forward(time, first_clip);
    }

    Clip* Track::seek_backward(double time, Clip* clip)
    {
        while (clip != &head_node && clip->min_time > time)
            clip = (Clip*)clip->prev;
        return clip;
    }

    Clip* Track::seek_forward(double time, Clip* clip)
    {
        while (clip != &tail_node && clip->min_time < time)
            clip = (Clip*)clip->next;
        return clip;
    }

    void Track::prepare_play(double position, double beat_duration)
    {
        play_time = beat_to_seconds(position, beat_duration);
        if (playhead_position == position) return;
        playhead_position = position;
        current_playing_clip = seek_adjacent_clip(position);
        last_played_clip = nullptr;
    }

    void Track::get_next_message(double tick_duration, double beat_duration, TrackMessage& message_return)
    {
        if (last_played_clip && play_time >= beat_to_seconds(last_played_clip->max_time, beat_duration)) {
            message_return.event = TrackMessageType::AudioEvent;
            message_return.timestamp = play_time;
            last_played_clip = nullptr;
        }

        if (current_playing_clip && current_playing_clip != &tail_node && play_time >= beat_to_seconds(current_playing_clip->min_time, beat_duration)) {
            last_played_clip = current_playing_clip;
            current_playing_clip = (Clip*)current_playing_clip->next;
            message_return.event = TrackMessageType::AudioEvent;
            message_return.timestamp = play_time;
        }

        play_time += tick_duration;
    }

    void Track::process(AudioBuffer<float>& output_buffer, double sample_rate, double tick_duration, bool is_playing)
    {
        if (is_playing) {

        }
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
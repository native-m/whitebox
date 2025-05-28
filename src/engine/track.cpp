#include "track.h"

#include <algorithm>

#include "assets_table.h"
#include "clip_edit.h"
#include "core/core_math.h"
#include "core/debug.h"
#include "core/panning_law.h"
#include "core/queue.h"
#include "dsp/dsp_ops.h"
#include "plughost/plugin_manager.h"

#ifndef _NDEBUG
#define WB_DBG_LOG_CLIP_ORDERING    1
#define WB_DBG_LOG_NOTE_EVENT       1
#define WB_DBG_LOG_AUDIO_EVENT      0
#define WB_DBG_LOG_PARAMETER_UPDATE 1
#endif

namespace wb {
Track::Track() {
  ui_parameter_state.volume = 1.0f;
  ui_parameter_state.pan = 0.0f;
  ui_parameter_state.mute = false;
  ui_param_transfer.set_capacity(64);
  ui_param_transfer.push({
    .id = TrackParameter_Volume,
    .value = 1.0,
  });
  ui_param_transfer.push({
    .id = TrackParameter_Pan,
    .value = 0.0,
  });
  ui_param_transfer.push({
    .id = TrackParameter_Mute,
    .value = 0.0,
  });
}

Track::Track(const std::string& name, const Color& color, float height, bool shown, const TrackParameterState& track_param)
    : name(name),
      color(color),
      height(height),
      shown(shown),
      ui_parameter_state(track_param) {
  ui_param_transfer.set_capacity(64);
  ui_param_transfer.push({
    .id = TrackParameter_Volume,
    .value = (double)math::db_to_linear(track_param.volume_db),
  });
  ui_param_transfer.push({
    .id = TrackParameter_Pan,
    .value = (double)ui_parameter_state.pan,
  });
  ui_param_transfer.push({
    .id = TrackParameter_Mute,
    .value = (double)ui_parameter_state.mute,
  });
}

Track::~Track() {
  for (auto clip : clips) {
    clip->~Clip();
    clip_allocator.free(clip);
  }
}

void Track::set_volume(float db) {
  ui_parameter_state.volume_db = db;
  ui_parameter_state.volume = math::db_to_linear(db);
  ui_param_transfer.push({
    .id = TrackParameter_Volume,
    .value = (double)ui_parameter_state.volume,
  });
}

void Track::set_pan(float pan) {
  ui_parameter_state.pan = pan;
  ui_param_transfer.push({
    .id = TrackParameter_Pan,
    .value = (double)ui_parameter_state.pan,
  });
}

void Track::set_mute(bool mute) {
  ui_parameter_state.mute = mute;
  ui_param_transfer.push({
    .id = TrackParameter_Mute,
    .value = (double)ui_parameter_state.mute,
  });
}

void Track::mark_clip_deleted(Clip* clip) {
  clip->mark_deleted();
  has_deleted_clips = true;
}

std::optional<ClipQueryResult> Track::query_clip_by_range(double min, double max) const {
  assert(min <= max && "Minimum value should be less or equal than maximum value");
  auto begin = clips.begin();
  auto end = clips.end();

  if (begin == end)
    return {};

  if (max <= (*begin)->min_time)
    return {};

  if (min >= clips.back()->max_time)
    return {};

  auto first = wb::find_lower_bound(begin, end, min, [](const Clip* clip, double time) { return clip->max_time <= time; });
  auto last = wb::find_lower_bound(begin, end, max, [](const Clip* clip, double time) { return clip->max_time <= time; });
  uint32_t first_clip = first - begin;
  uint32_t last_clip = last - begin;
  double first_offset;
  double last_offset;

  if (first == last && (max <= (*first)->min_time || min >= (*last)->max_time)) {
    return {};
  }

  if (min > (*first)->max_time) {
    first_clip++;
    first_offset = min - clips[first_clip]->min_time;
  } else {
    first_offset = min - (*first)->min_time;
  }

  if (max > (*last)->min_time) {
    last_offset = max - (*last)->max_time;
  } else {
    last_clip--;
    last_offset = max - clips[last_clip]->max_time;
  }

  return ClipQueryResult{
    .first = first_clip,
    .last = last_clip,
    .first_offset = first_offset,
    .last_offset = last_offset,
  };
}

void Track::update_clip_ordering() {
  Vector<Clip*> new_cliplist;
  if (has_deleted_clips) {
    for (auto clip : clips) {
      if (clip->is_deleted()) {
        deleted_clips.push_back(clip);
        continue;
      }
      new_cliplist.push_back(clip);
    }
    has_deleted_clips = false;
    clips = std::move(new_cliplist);
    for (auto clip : deleted_clips) {
      destroy_clip(clip);
    }
    deleted_clips.resize(0);
  }
  std::sort(clips.begin(), clips.end(), [](const Clip* a, const Clip* b) { return a->min_time < b->min_time; });
  for (uint32_t i = 0; i < (uint32_t)clips.size(); i++) {
    clips[i]->id = i;
  }
}

std::optional<uint32_t> Track::find_next_clip(double time_pos, uint32_t hint) {
  if (clips.size() == 0) {
    return {};
  }

  if (clips.back()->max_time < time_pos) {
    return {};
  }

  auto begin = clips.begin();
  auto end = clips.end();

#if 0
  if (end - begin <= 64) {
    while (begin != end && time_pos >= (*begin)->max_time) {
      begin++;
    }
    if (begin == end) {
      return {};
    }
    return (*begin)->id;
  }
#endif

  auto clip = find_lower_bound(begin, end, time_pos, [](Clip* clip, double time_pos) { return clip->max_time <= time_pos; });

  if (clip == end) {
    return {};
  }

  return (*clip)->id;
}

void Track::prepare_effect_buffer(uint32_t num_channels, uint32_t num_samples) {
  effect_buffer.resize(num_samples);
  effect_buffer.resize_channel(num_channels);
}

void Track::reset_playback_state(double time_pos, bool refresh_voices) {
  if (!refresh_voices) {
    std::optional<uint32_t> next_clip = find_next_clip(time_pos);
    event_state.current_clip_idx.reset();
    event_state.current_clip = nullptr;
    event_state.clip_idx = next_clip;
    event_state.midi_note_idx = 0;
    event_state.partially_ended = false;
    midi_voice_state.voice_mask = 0;
    midi_voice_state.release_all();
  }
  event_state.refresh_voice = refresh_voices;
}

void Track::prepare_record(double time_pos) {
  if (!input_attr.armed || input.type == TrackInputType::None)
    return;
  record_min_time = time_pos;
  record_max_time = time_pos;
  input_attr.recording = true;
}

void Track::stop_record() {
  record_min_time = 0.0;
  record_max_time = 0.0;
  input_attr.recording = false;
}

void Track::stop() {
  current_audio_event = {
    .type = EventType::None,
  };
  audio_event_buffer.resize(0);
  midi_event_list.clear();
  stop_record();
  // midi_voice_state.voice_mask = 0;
}

void Track::process_event(
    double start_time,
    double end_time,
    double sample_position,
    double beat_duration,
    double buffer_duration,
    double sample_rate,
    double ppq,
    double inv_ppq,
    uint32_t buffer_size) {
  if (clips.size() == 0) {
    if (event_state.refresh_voice) {
      audio_event_buffer.push_back({
        .type = EventType::StopSample,
        .buffer_offset = 0,
        .time = start_time,
      });
      kill_all_voices(0, start_time);
      event_state.current_clip_idx.reset();
      event_state.clip_idx.reset();
      event_state.midi_note_idx = 0;
      event_state.refresh_voice = false;
    }
    if (input_attr.recording)
      record_max_time += buffer_duration;
    return;
  }

  uint32_t num_clips = (uint32_t)clips.size();
  if (event_state.refresh_voice) [[unlikely]] {
    std::optional<uint32_t> clip_at_playhead = find_next_clip(start_time);
    // TODO: Skip if refreshing the same clip
    if (clip_at_playhead) {
      if (event_state.clip_idx) {
        uint32_t idx = *event_state.clip_idx;
        if (idx < num_clips) {
          Clip* clip = clips[*clip_at_playhead];
          Clip* current_clip = clips[idx];
          if (clip != current_clip && start_time >= clip->min_time && start_time <= clip->max_time) {
            if (clip->is_audio()) {
              audio_event_buffer.push_back({
                .type = EventType::StopSample,
                .buffer_offset = 0,
                .time = start_time,
              });
            } else {
              kill_all_voices(0, start_time);
            }
            event_state.clip_idx = *clip_at_playhead;
            event_state.midi_note_idx = 0;
            event_state.partially_ended = false;
          } else if (clip == current_clip && (start_time < clip->min_time || start_time > clip->max_time)) {
            if (clip->is_audio()) {
              audio_event_buffer.push_back({
                .type = EventType::StopSample,
                .buffer_offset = 0,
                .time = start_time,
              });
            } else {
              kill_all_voices(0, start_time);
            }
            event_state.clip_idx = *clip_at_playhead;
            event_state.midi_note_idx = 0;
            event_state.partially_ended = false;
          }
        }
      } else {
        event_state.clip_idx = *clip_at_playhead;
        event_state.midi_note_idx = 0;
      }
    } else {
      audio_event_buffer.push_back({
        .type = EventType::StopSample,
        .buffer_offset = 0,
        .time = start_time,
      });
      kill_all_voices(0, start_time);
      event_state.clip_idx.reset();
      event_state.midi_note_idx = 0;
    }
    event_state.refresh_voice = false;
  }

  if (!event_state.clip_idx) {
    if (input_attr.recording)
      record_max_time += buffer_duration;
    return;
  }

  uint32_t next_clip = *event_state.clip_idx;
  while (next_clip < num_clips) {
    Clip* clip = clips[next_clip];
    double min_time = clip->min_time;
    double max_time = clip->max_time;

    if (min_time > end_time)
      break;

    bool is_audio = clip->is_audio();
    if (min_time >= start_time) {  // Started from beginning
      if (is_audio) {
        double offset_from_start = beat_to_samples(min_time - start_time, sample_rate, beat_duration);
        double sample_offset = sample_position + offset_from_start;
        uint32_t buffer_offset = (uint32_t)((uint64_t)sample_offset % (uint64_t)buffer_size);
        audio_event_buffer.push_back({
          .type = EventType::PlaySample,
          .buffer_offset = buffer_offset,
          .time = min_time,
          .sample_offset = (size_t)clip->start_offset,
          .clip = clip,
          .sample = &clip->audio.asset->sample_instance,
        });
      } else {
        event_state.midi_note_idx = clip->midi.asset->find_first_note(clip->start_offset, 0);
      }
      clip->start_offset_changed = false;
    } else if (start_time > min_time && !event_state.partially_ended) {  // Partially started (started in the middle)
      double relative_start_time = start_time - min_time;
      if (is_audio) {
        double sample_pos = beat_to_samples(relative_start_time, sample_rate, beat_duration);
        size_t sample_offset = (size_t)(sample_pos + clip->start_offset);
        audio_event_buffer.push_back({
          .type = EventType::PlaySample,
          .buffer_offset = 0,
          .time = start_time,
          .sample_offset = sample_offset,
          .sample = &clip->audio.asset->sample_instance,
        });
      } else {
        double actual_start_offset = relative_start_time + clip->start_offset;
        event_state.midi_note_idx = clip->midi.asset->find_first_note(actual_start_offset, 0);
      }
      clip->start_offset_changed = false;
    } else if (clip->start_offset_changed && event_state.partially_ended) {
      double relative_start_time = start_time - min_time;
      if (is_audio) {
        double sample_pos = beat_to_samples(relative_start_time, sample_rate, beat_duration);
        size_t sample_offset = (size_t)(sample_pos + clip->start_offset);
        audio_event_buffer.push_back({
          .type = EventType::StopSample,
          .buffer_offset = 0,
          .time = start_time,
        });
        audio_event_buffer.push_back({
          .type = EventType::PlaySample,
          .buffer_offset = 0,
          .time = start_time,
          .sample_offset = sample_offset,
          .clip = clip,
          .sample = &clip->audio.asset->sample_instance,
        });
      } else {
        kill_all_voices(0, start_time);
        double actual_start_offset = relative_start_time + clip->start_offset;
        event_state.midi_note_idx = clip->midi.asset->find_first_note(actual_start_offset, 0);
      }
      clip->start_offset_changed = false;
    }

    if (max_time <= end_time) {  // Reaching the end of the clip
      if (is_audio) {
        double offset_from_start = beat_to_samples(max_time - start_time, sample_rate, beat_duration);
        double sample_offset = sample_position + offset_from_start;
        uint32_t buffer_offset = (uint32_t)((uint64_t)sample_offset % (uint64_t)buffer_size);
        audio_event_buffer.push_back({
          .type = EventType::StopSample,
          .buffer_offset = buffer_offset,
          .time = max_time,
        });
      } else {
        process_midi_event(
            clip, start_time, max_time, sample_position, beat_duration, sample_rate, ppq, inv_ppq, buffer_size);
      }
      event_state.partially_ended = false;
    } else {
      if (!is_audio) {
        process_midi_event(
            clip, start_time, end_time, sample_position, beat_duration, sample_rate, ppq, inv_ppq, buffer_size);
      }
      event_state.partially_ended = true;
      break;
    }

    next_clip++;
  }

  if (input_attr.recording)
    record_max_time += buffer_duration;
  event_state.clip_idx = next_clip;
}

void Track::process_midi_event(
    Clip* clip,
    double start_time,
    double end_time,
    double sample_position,
    double beat_duration,
    double sample_rate,
    double ppq,
    double inv_ppq,
    uint32_t buffer_size) {
  MidiAsset* asset = clip->midi.asset;
  const MidiNoteBuffer& buffer = asset->data.note_sequence;
  uint32_t midi_note_idx = event_state.midi_note_idx;
  uint32_t note_count = (uint32_t)buffer.size();
  double max_clip_time = clip->max_time;
  double time_offset = clip->min_time - clip->start_offset;
  double mult = 1.0 / (double)clip->midi.rate;

  while (midi_note_idx < note_count) {
    const MidiNote& note = buffer[midi_note_idx];
    double min_time = time_offset + note.min_time * mult;  // math::round((time_offset + note.min_time) * ppq) * inv_ppq;
    double max_time = math::min(
        time_offset + note.max_time * mult, max_clip_time);  // math::round((time_offset + note.max_time) * ppq) * inv_ppq;

    if (min_time > end_time || min_time >= clip->max_time)
      break;

    while (auto voice = midi_voice_state.release_voice(min_time)) {
      double offset_from_start = beat_to_samples(voice->max_time - start_time, sample_rate, beat_duration);
      double sample_offset = sample_position + offset_from_start;
      uint32_t buffer_offset = (uint32_t)((uint64_t)sample_offset % (uint64_t)buffer_size);
      midi_event_list.add_event({
        .type = MidiEventType::NoteOff,
        .buffer_offset = buffer_offset,
        .time = voice->max_time,
        .note_off = {
          .channel = 0,
          .key = voice->key,
          .velocity = voice->velocity,
        },
      });
#if WB_DBG_LOG_NOTE_EVENT
      char note_str[8]{};
      fmt::format_to_n(
          note_str, std::size(note_str), "{}{}", get_midi_note_scale(voice->key), get_midi_note_octave(voice->key));
      Log::debug("Note off: {} length: {} at: {}", note_str, voice->max_time, buffer_offset);
#endif
    }

    double offset_from_start = beat_to_samples(min_time - start_time, sample_rate, beat_duration);
    double sample_offset = sample_position + offset_from_start;
    uint32_t buffer_offset = (uint32_t)((uint64_t)sample_offset % (uint64_t)buffer_size);

    bool voice_added = midi_voice_state.add_voice({
      .max_time = max_time,
      .velocity = note.velocity,
      .channel = 0,
      .key = note.key,
    });

    // Skip if we have reached maximum voices
    if (!voice_added || contain_bit(note.flags, MidiNoteFlags::Muted)) {
      midi_note_idx++;
      continue;
    }

    midi_event_list.add_event({
      .type = MidiEventType::NoteOn,
      .buffer_offset = buffer_offset,
      .time = min_time,
      .note_on = {
        .channel = 0,
        .key = note.key,
        .velocity = note.velocity,
      },
    });

#if WB_DBG_LOG_NOTE_EVENT
    char note_str[8]{};
    fmt::format_to_n(note_str, std::size(note_str), "{}{}", get_midi_note_scale(note.key), get_midi_note_octave(note.key));
    Log::debug("Note on: {} {} -> {} at {}", note_str, min_time, max_time, buffer_offset);
#endif

    midi_note_idx++;
  }

  while (auto voice = midi_voice_state.release_voice(end_time)) {
    double offset_from_start = beat_to_samples(voice->max_time - start_time, sample_rate, beat_duration);
    double sample_offset = sample_position + offset_from_start;
    uint32_t buffer_offset = (uint32_t)((uint64_t)sample_offset % (uint64_t)buffer_size);
    midi_event_list.add_event({
      .type = MidiEventType::NoteOff,
      .buffer_offset = buffer_offset,
      .time = voice->max_time,
      .note_off = {
        .channel = 0,
        .key = voice->key,
        .velocity = voice->velocity,
      },
    });
#if WB_DBG_LOG_NOTE_EVENT
    char note_str[8]{};
    fmt::format_to_n(
        note_str, std::size(note_str), "{}{}", get_midi_note_scale(voice->key), get_midi_note_octave(voice->key));
    Log::debug("Note off: {} length: {} at: {}", note_str, voice->max_time, buffer_offset);
#endif
  }

  event_state.midi_note_idx = midi_note_idx;
}

void Track::kill_all_voices(uint32_t buffer_offset, double time_pos) {
  while (auto voice = midi_voice_state.release_voice(std::numeric_limits<double>::max())) {
    midi_event_list.add_event({
      .type = MidiEventType::NoteOff,
      .buffer_offset = buffer_offset,
      .time = time_pos,
      .note_off = {
        .channel = 0,
        .key = voice->key,
        .velocity = voice->velocity,
      },
    });
  }
}

void Track::process(
    const AudioBuffer<float>& input_buffer,
    AudioBuffer<float>& output_buffer,
    double sample_rate,
    double beat_duration,
    double playhead_pos,
    int64_t playhead_in_samples,
    bool playing) {
  AudioBuffer<float>& write_buffer = plugin_instance ? effect_buffer : output_buffer;
  transfer_param_changes();

  for (uint32_t i = 0; i < param_queue.values.size(); i++) {
    dsp::ParamValue& value = param_queue.values[i];
    switch (value.id) {
      case TrackParameter_Volume: parameter_state.volume = (float)value.value;
#ifdef WB_DBG_LOG_PARAMETER_UPDATE
        Log::debug("Volume changed: {} {}", parameter_state.volume, math::linear_to_db(parameter_state.volume));
#endif
        break;
      case TrackParameter_Pan: {
        parameter_state.pan = (float)value.value;
        PanningCoefficient pan = calculate_panning_coefs(parameter_state.pan, PanningLaw::ConstantPower_3db);
        parameter_state.pan_coeffs[0] = pan.left;
        parameter_state.pan_coeffs[1] = pan.right;
#ifdef WB_DBG_LOG_PARAMETER_UPDATE
        Log::debug(
            "Pan changed: {} {} {}", parameter_state.pan, parameter_state.pan_coeffs[0], parameter_state.pan_coeffs[1]);
#endif
        break;
      }
      case TrackParameter_Mute: parameter_state.mute = value.value > 0.0 ? 1.0f : 0.0f;
#ifdef WB_DBG_LOG_PARAMETER_UPDATE
        Log::debug("Mute changed: {}", parameter_state.mute);
#endif
        break;
    }
  }

  if (plugin_instance)
    write_buffer.clear();

  if (playing) {
    AudioEvent* event = audio_event_buffer.begin();
    AudioEvent* end = audio_event_buffer.end();
    uint32_t start_sample = 0;
    while (start_sample < write_buffer.n_samples) {
      if (event != end) {
        uint32_t event_length = event->buffer_offset - start_sample;
        float gain = event->clip ? event->clip->audio.gain : 1.0f;
        render_sample(write_buffer, gain, start_sample, event_length, sample_rate);
#if WB_DBG_LOG_AUDIO_EVENT
        switch (event->type) {
          case EventType::StopSample: Log::debug("{}: Stop {} {}", name, event->time, event->buffer_offset); break;
          case EventType::PlaySample: Log::debug("{}: Play {} {}", name, event->time, event->buffer_offset); break;
          default: break;
        }
#endif
        current_audio_event = *event;
        start_sample += event_length;
        event++;
      } else {
        uint32_t event_length = write_buffer.n_samples - start_sample;
        float gain = current_audio_event.clip ? current_audio_event.clip->audio.gain : 1.0f;
        render_sample(write_buffer, gain, start_sample, event_length, sample_rate);
        start_sample = write_buffer.n_samples;
      }
    }
  }

  // process_test_synth(write_buffer, sample_rate, playing);

  if (plugin_instance) {
    PluginProcessInfo process_info;
    process_info.sample_count = output_buffer.n_samples;
    process_info.input_buffer_count = 1;
    process_info.output_buffer_count = 1;
    process_info.input_buffer = &write_buffer;
    process_info.output_buffer = &output_buffer;
    process_info.input_event_list = &midi_event_list;
    process_info.sample_rate = sample_rate;
    process_info.tempo = 60.0 / beat_duration;
    process_info.project_time_in_ppq = playhead_pos;
    process_info.project_time_in_samples = playhead_in_samples;
    process_info.playing = playing;
    plugin_instance->process(process_info);
  }

  float volume = parameter_state.mute ? 0.0f : parameter_state.volume;
  for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
    float* buf = output_buffer.channel_buffers[i];
    dsp::apply_gain(buf, output_buffer.n_samples, volume * parameter_state.pan_coeffs[i]);
    level_meter[i].push_samples(output_buffer, i);
  }

  param_queue.clear();
}

void Track::render_sample(
    AudioBuffer<float>& output_buffer,
    float gain,
    uint32_t buffer_offset,
    uint32_t num_samples,
    double sample_rate) {
  switch (current_audio_event.type) {
    case EventType::None:
    case EventType::StopSample: samples_processed = 0; break;
    case EventType::PlaySample: {
      Sample* sample = current_audio_event.sample;
      size_t sample_offset = samples_processed + current_audio_event.sample_offset;
      if (sample_offset >= sample->count)
        break;
      uint32_t min_num_samples = std::min(num_samples, (uint32_t)(sample->count - sample_offset));
      stream_sample(output_buffer, current_audio_event.sample, gain, buffer_offset, min_num_samples, sample_offset);
      samples_processed += min_num_samples;
      break;
    }
    default: break;
  }
}

void Track::stream_sample(
    AudioBuffer<float>& output_buffer,
    Sample* sample,
    float gain,
    uint32_t buffer_offset,
    uint32_t num_samples,
    size_t sample_offset) {
  static constexpr float i16_pcm_normalizer = 1.0f / static_cast<float>(std::numeric_limits<int16_t>::max());
  static constexpr double i24_pcm_normalizer = 1.0 / static_cast<double>((1 << 23) - 1);
  static constexpr double i32_pcm_normalizer = 1.0 / static_cast<double>(std::numeric_limits<int32_t>::max());

  switch (sample->format) {
    case AudioFormat::I16:
      for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
        float* output = output_buffer.get_write_pointer(i);
        auto sample_data = sample->get_read_pointer<int16_t>(i % sample->channels);
        for (uint32_t j = 0; j < num_samples; j++) {
          float sample = (float)sample_data[sample_offset + j] * i16_pcm_normalizer;
          output[j + buffer_offset] += math::clamp(sample * gain, -1.0f, 1.0f);
        }
      }
      break;
    case AudioFormat::I24:
      for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
        float* output = output_buffer.get_write_pointer(i);
        auto sample_data = sample->get_read_pointer<int32_t>(i % sample->channels);
        for (uint32_t j = 0; j < num_samples; j++) {
          double sample = (double)sample_data[sample_offset + j] * i24_pcm_normalizer;
          output[j + buffer_offset] += math::clamp((float)sample * gain, -1.0f, 1.0f);
        }
      }
      break;
    case AudioFormat::I32:
      for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
        float* output = output_buffer.get_write_pointer(i);
        auto sample_data = sample->get_read_pointer<int32_t>(i % sample->channels);
        for (uint32_t j = 0; j < num_samples; j++) {
          double sample = (double)sample_data[sample_offset + j] * i32_pcm_normalizer;
          output[j + buffer_offset] += math::clamp((float)sample * gain, -1.0f, 1.0f);
        }
      }
      break;
    case AudioFormat::F32:
      for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
        float* output = output_buffer.get_write_pointer(i);
        auto sample_data = sample->get_read_pointer<float>(i % sample->channels);
        for (uint32_t j = 0; j < num_samples; j++) {
          output[j + buffer_offset] += sample_data[sample_offset + j] * gain;
        }
      }
      break;
    case AudioFormat::F64:
      for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
        float* output = output_buffer.get_write_pointer(i);
        auto sample_data = sample->get_read_pointer<double>(i % sample->channels);
        for (uint32_t j = 0; j < num_samples; j++) {
          output[j + buffer_offset] += (float)sample_data[sample_offset + j] * gain;
        }
      }
      break;
    default: assert(false && "Unsupported format"); break;
  }
}

// This code is only made for testing purposes, the code will be removed later
void Track::process_test_synth(AudioBuffer<float>& output_buffer, double sample_rate, bool playing) {
  uint32_t event_idx = 0;
  uint32_t event_count = midi_event_list.size();
  uint32_t start_sample = 0;
  uint32_t next_buffer_offset = 0;
  while (start_sample < output_buffer.n_samples) {
    if (event_idx < event_count) {
      MidiEvent& event = midi_event_list.get_event(event_idx);

      // Continue until the next event
      uint32_t event_length = event.buffer_offset - start_sample;
      test_synth.render(output_buffer, sample_rate, start_sample, event_length);
      start_sample += event_length;

      // Set next state
      for (; event_idx < event_count; event_idx++) {
        MidiEvent& event = midi_event_list.get_event(event_idx);
        if (event.buffer_offset > start_sample) {
          break;
        }
        switch (event.type) {
          case MidiEventType::NoteOn: test_synth.add_voice(event); break;
          case MidiEventType::NoteOff: test_synth.remove_note(event.note_off.key); break;
          default: break;
        }
        // Log::debug("{}", test_synth.voice_mask);
      }
    } else {
      test_synth.render(output_buffer, sample_rate, start_sample, output_buffer.n_samples - start_sample);
      start_sample = output_buffer.n_samples;
    }
  }
}

void Track::transfer_param_changes() {
  TrackParamTransfer tf;
  while (ui_param_transfer.pop(tf)) {
    if (tf.plugin) {
      tf.plugin->transfer_param(tf.id, tf.value);
    } else {
      param_queue.push_back_value(0, tf.id, tf.value);
    }
  }
}

PluginResult Track::plugin_begin_edit(void* userdata, PluginInterface* plugin, uint32_t param_id) {
  Track* track = (Track*)userdata;
  Log::debug("beginEdit called ({})", param_id);
  return PluginResult::Ok;
}

PluginResult
Track::plugin_perform_edit(void* userdata, PluginInterface* plugin, uint32_t param_id, double normalized_value) {
  Track* track = (Track*)userdata;
  track->ui_param_transfer.push({
    .id = param_id,
    .plugin = plugin,
    .value = normalized_value,
  });
  return PluginResult::Ok;
}

PluginResult Track::plugin_end_edit(void* userdata, PluginInterface* plugin, uint32_t param_id) {
  Track* track = (Track*)userdata;
  Log::debug("endEdit called ({})", param_id);
  return PluginResult::Ok;
}

}  // namespace wb
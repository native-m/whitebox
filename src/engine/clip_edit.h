#pragma once

#include "clip.h"
#include "core/common.h"
#include "core/core_math.h"
#include "etypes.h"

namespace wb {

static inline ClipMoveResult calc_move_clip(Clip* clip, double relative_pos, double min_move = 0.0) {
  const double new_pos = math::max(clip->min_time + relative_pos, min_move);
  return {
    new_pos,
    new_pos + (clip->max_time - clip->min_time),
  };
}

static inline ClipResizeResult calc_resize_clip(
    Clip* clip,
    double relative_pos,
    double resize_limit,
    double min_length,
    double min_resize_pos,
    double beat_duration,
    bool is_min,
    bool shift = false,
    bool stretch = false,
    bool clamp_at_resize_pos = false) {
  if (!is_min) {
    const double old_max = clip->max_time;
    const double actual_min_length = resize_limit + min_length - clip->min_time;
    double new_max = math::max(clip->max_time + relative_pos, 0.0);
    double length = new_max - clip->min_time;
    if (length < actual_min_length)
      new_max = clip->min_time + actual_min_length;

    double start_offset = clip->start_offset;
    double new_speed = 1.0;

    if (shift) {
      SampleAsset* asset = nullptr;
      if (clip->is_audio()) {
        asset = clip->audio.asset;
        start_offset = samples_to_beat(start_offset, (double)asset->sample_instance.sample_rate, beat_duration);
      }
      double new_max_clamped = math::min(new_max, clip->max_time);
      if (old_max < new_max)
        start_offset -= new_max_clamped - old_max;
      else
        start_offset += old_max - new_max_clamped;
      if (clip->is_audio() && asset)
        start_offset = beat_to_samples(start_offset, (double)asset->sample_instance.sample_rate, beat_duration);
    }

    if (stretch && clip->is_audio()) {
      SampleAsset* asset = clip->audio.asset;
      if (asset) {
        double sample_count = (double)asset->sample_instance.count;
        double old_length = sample_count / clip->audio.speed;
        double num_samples = beat_to_samples(relative_pos, clip->get_asset_sample_rate(), beat_duration);
        new_speed = sample_count / (old_length + num_samples);
      }
    }

    return {
      .min = clip->min_time,
      .max = new_max,
      .start_offset = start_offset,
      .speed = new_speed,
    };
  }

  const double old_min = clip->min_time;
  const double actual_min_length = clip->max_time - resize_limit + min_length;
  double new_min = math::max(clip->min_time + relative_pos, 0.0);
  double length = clip->max_time - new_min;
  if (length < actual_min_length)
    new_min = clip->max_time - actual_min_length;
  if (clamp_at_resize_pos && new_min < min_resize_pos)
    new_min = min_resize_pos;

  double start_offset = clip->start_offset;
  double new_speed = 1.0;

  if (!shift) {
    double old_start_offset = start_offset;
    SampleAsset* asset = nullptr;
    if (clip->is_audio()) {
      asset = clip->audio.asset;
      start_offset = samples_to_beat(start_offset, (double)asset->sample_instance.sample_rate, beat_duration);
    }

    if (old_min < new_min)
      start_offset -= old_min - new_min;
    else
      start_offset += new_min - old_min;

    if (start_offset < 0.0)
      new_min = new_min - start_offset;

    start_offset = math::max(start_offset, 0.0);
    if (clip->is_audio() && asset)
      start_offset = beat_to_samples(start_offset, (double)asset->sample_instance.sample_rate, beat_duration);
  }

  if (stretch && clip->is_audio()) {
    SampleAsset* asset = clip->audio.asset;
    if (asset) {
      double sample_count = (double)asset->sample_instance.count;
      double old_length = sample_count / clip->audio.speed;
      double num_samples = beat_to_samples(old_min - new_min, clip->get_asset_sample_rate(), beat_duration);
      new_speed = sample_count / (old_length + num_samples);
    }
  }

  return {
    .min = new_min,
    .max = clip->max_time,
    .start_offset = start_offset,
    .speed = new_speed,
  };
}

static double
calc_clip_shift(bool is_audio_clip, double start_offset, double relative_pos, double beat_duration, double sample_rate) {
  if (is_audio_clip) {
    const double offset_in_beat = samples_to_beat(start_offset, sample_rate, beat_duration);
    return beat_to_samples(math::max(offset_in_beat - relative_pos, 0.0), sample_rate, beat_duration);
  }

  const double offset = start_offset;
  return math::max(offset - relative_pos, 0.0);
}

static double shift_clip_content(Clip* clip, double relative_pos, double beat_duration) {
  bool is_audio_clip = clip->is_audio();
  double sample_rate = 0.0;

  if (is_audio_clip) {
    SampleAsset* asset = clip->audio.asset;
    sample_rate = (double)asset->sample_instance.sample_rate;
  }

  return calc_clip_shift(is_audio_clip, clip->start_offset, relative_pos, beat_duration, sample_rate);
}

}  // namespace wb

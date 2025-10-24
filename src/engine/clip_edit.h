#pragma once

#include "clip.h"
#include "core/common.h"
#include "core/core_math.h"
#include "etypes.h"

namespace wb {

enum class ClipResizeMode {
  Resize,
  Stretch,
  Nudge,
};

static inline ClipMoveResult calc_move_clip(Clip* clip, double relative_ofs, double min_move = 0.0) {
  const double new_pos = math::max(clip->min_time + relative_ofs, min_move);
  return {
    new_pos,
    new_pos + (clip->max_time - clip->min_time),
  };
}

static inline ClipResizeResult calc_resize_clip(
    Clip* clip,
    double relative_ofs,
    double min_length,
    double min_resize_pos,
    double beat_duration,
    bool left,
    ClipResizeMode mode = ClipResizeMode::Resize) {
  bool is_audio = clip->is_audio();

  // MIDI clip cannot be streched
  if (mode == ClipResizeMode::Stretch && !is_audio) {
    mode = ClipResizeMode::Resize;
  }

  if (!left) {
    const double old_end_pos = clip->max_time;
    double actual_min_length = min_resize_pos + min_length - clip->min_time;
    double new_end_pos = math::max(clip->max_time + relative_ofs, 0.0);
    double length = new_end_pos - clip->min_time;
    double speed = is_audio ? clip->audio.speed : 1.0;

    if (length < actual_min_length) {
      new_end_pos = clip->min_time + actual_min_length;
    }

    if (mode == ClipResizeMode::Stretch) {
      AudioAsset* asset = clip->audio.asset;
      if (asset) {
        double sample_rate = (double)asset->sample.sample_rate;
        double sample_count = (double)asset->sample.count;
        double old_length = sample_count / speed;
        double num_samples = beat_to_samples(new_end_pos - old_end_pos, sample_rate, beat_duration);
        speed = sample_count / (old_length + num_samples);
      }
    }

    return {
      .min = clip->min_time,
      .max = new_end_pos,
      .start_offset = clip->start_offset,
      .speed = speed,
    };
  }

  const double old_start_pos = clip->min_time;
  double actual_min_length = clip->max_time - min_resize_pos + min_length;
  double new_start_pos = math::max(clip->min_time + relative_ofs, 0.0);
  double length = clip->max_time - new_start_pos;
  double start_offset = clip->start_offset;
  double speed = is_audio ? clip->audio.speed : 1.0;

  if (length < actual_min_length) {
    new_start_pos = clip->max_time - actual_min_length;
  }

  if (mode == ClipResizeMode::Resize) {
    double sample_rate = 0.0;

    if (is_audio) {
      sample_rate = clip->get_asset_sample_rate();
      start_offset = samples_to_beat(start_offset, sample_rate, beat_duration);
    }

    start_offset += new_start_pos - old_start_pos;

    if (start_offset < 0.0) {
      new_start_pos = new_start_pos - start_offset;
    }

    if (is_audio) {
      start_offset = beat_to_samples(start_offset, sample_rate, beat_duration);
    }
  } else if (mode == ClipResizeMode::Stretch) {
    AudioAsset* asset = clip->audio.asset;
    if (asset) {
      double sample_rate = (double)asset->sample.sample_rate;
      double sample_count = (double)asset->sample.count;
      double old_length = sample_count / speed;
      double num_samples = beat_to_samples(old_start_pos - new_start_pos, sample_rate, beat_duration);
      speed = sample_count / (old_length + num_samples);
    }
  }

  return {
    .min = new_start_pos,
    .max = clip->max_time,
    .start_offset = start_offset,
    .speed = speed,
  };
}

static inline ClipResizeResult calc_resize_clip(
    Clip* clip,
    double relative_ofs,
    double resize_limit,
    double min_length,
    double min_resize_pos,
    double beat_duration,
    bool left,
    bool shift = false,
    bool stretch = false,
    bool clamp_at_resize_pos = false) {
  if (!left) {
    const double old_end_pos = clip->max_time;
    const double actual_min_length = resize_limit + min_length - clip->min_time;
    double new_end_pos = math::max(clip->max_time + relative_ofs, 0.0);
    double length = new_end_pos - clip->min_time;

    if (length < actual_min_length)
      new_end_pos = clip->min_time + actual_min_length;

    double start_offset = clip->start_offset;
    double new_speed = 1.0;

    if (shift) {
      AudioAsset* asset = nullptr;
      double mult = 1.0;

      if (clip->is_audio()) {
        asset = clip->audio.asset;
        mult = clip->audio.speed;
        start_offset = samples_to_beat(start_offset, (double)asset->sample.sample_rate, beat_duration);
      }

      if (old_end_pos < new_end_pos) {
        start_offset -= (new_end_pos - old_end_pos) * mult;
      } else {
        start_offset += (old_end_pos - new_end_pos) * mult;
      }

      start_offset = math::max(start_offset, 0.0);

      if (clip->is_audio() && asset) {
        start_offset = math::min(start_offset, (double)asset->sample.count);
        start_offset = beat_to_samples(start_offset, (double)asset->sample.sample_rate, beat_duration);
      }
    }

    if (stretch && clip->is_audio()) {
      AudioAsset* asset = clip->audio.asset;
      if (asset) {
        double sample_count = (double)asset->sample.count;
        double old_length = sample_count / clip->audio.speed;
        double num_samples = beat_to_samples(relative_ofs, clip->get_asset_sample_rate(), beat_duration);
        new_speed = sample_count / (old_length + num_samples);
      }
    }

    return {
      .min = clip->min_time,
      .max = new_end_pos,
      .start_offset = start_offset,
      .speed = new_speed,
    };
  }

  const double old_start_pos = clip->min_time;
  const double actual_min_length = clip->max_time - resize_limit + min_length;
  double new_start_pos = math::max(clip->min_time + relative_ofs, 0.0);
  double length = clip->max_time - new_start_pos;

  if (length < actual_min_length) {
    new_start_pos = clip->max_time - actual_min_length;
  }

  if (clamp_at_resize_pos && new_start_pos < min_resize_pos) {
    new_start_pos = min_resize_pos;
  }

  double start_offset = clip->start_offset;
  double new_speed = 1.0;

  if (!shift) {
    double old_start_offset = start_offset;
    AudioAsset* asset = nullptr;

    if (clip->is_audio()) {
      asset = clip->audio.asset;
      start_offset = samples_to_beat(start_offset, (double)asset->sample.sample_rate, beat_duration);
    }

    if (old_start_pos < new_start_pos) {
      start_offset -= old_start_pos - new_start_pos;
    } else {
      start_offset += new_start_pos - old_start_pos;
    }

    if (start_offset < 0.0) {
      new_start_pos = new_start_pos - start_offset;
    }

    start_offset = math::max(start_offset, 0.0);

    if (clip->is_audio() && asset) {
      start_offset = beat_to_samples(start_offset, (double)asset->sample.sample_rate, beat_duration);
    }
  }

  if (stretch && clip->is_audio()) {
    AudioAsset* asset = clip->audio.asset;
    if (asset) {
      double sample_count = (double)asset->sample.count;
      double old_length = sample_count / clip->audio.speed;
      double num_samples = beat_to_samples(old_start_pos - new_start_pos, clip->get_asset_sample_rate(), beat_duration);
      new_speed = sample_count / (old_length + num_samples);
    }
  }

  return {
    .min = new_start_pos,
    .max = clip->max_time,
    .start_offset = start_offset,
    .speed = new_speed,
  };
}

static double calc_clip_shift(
    bool is_audio_clip,
    double start_offset,
    double relative_ofs,
    double beat_duration,
    double sample_rate,
    double speed = 1.0) {
  if (is_audio_clip) {
    const double offset_in_beat = samples_to_beat(start_offset, sample_rate, beat_duration);
    return beat_to_samples(math::max(offset_in_beat - relative_ofs, 0.0), sample_rate, beat_duration);
  }

  const double offset = start_offset;
  return math::max(offset - relative_ofs, 0.0);
}

static double shift_clip_content(Clip* clip, double relative_ofs, double beat_duration) {
  bool is_audio_clip = clip->is_audio();
  double sample_rate = 0.0;
  double speed = 1.0;

  if (is_audio_clip) {
    AudioAsset* asset = clip->audio.asset;
    sample_rate = (double)asset->sample.sample_rate;
    speed = clip->audio.speed;
  }

  return calc_clip_shift(is_audio_clip, clip->start_offset, relative_ofs, beat_duration, sample_rate, speed);
}

}  // namespace wb

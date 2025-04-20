#pragma once

#include "core/common.h"
#include "core/midi.h"

namespace wb {

struct Clip;
struct Track;

enum class ClipSelectStatus {
  NotSelected,
  Selected,
  PartiallySelected,
};

struct ClipMoveResult {
  double min;
  double max;
};

struct ClipResizeResult {
  double min;
  double max;
  double start_offset;
};

struct TrackClipResizeInfo {
  bool should_resize;
  uint32_t clip_id;
};

struct ClipQueryResult {
  uint32_t first;
  uint32_t last;
  double first_offset;
  double last_offset;

  bool right_side_partially_selected(uint32_t id) const {
    return first == id && first_offset > 0.0;
  }

  bool left_side_partially_selected(uint32_t id) const {
    return last == id && last_offset < 0.0;
  }

  uint32_t num_clips() const {
    return (last - first) + 1;
  }
};

struct SelectedTrackRegion {
  bool has_clip_selected;
  ClipQueryResult range;

  ClipSelectStatus is_clip_selected(uint32_t id) const {
    if (math::in_range(id, range.first, range.last)) {
      if (id == range.first && range.first_offset > 0.0) {
        return ClipSelectStatus::PartiallySelected;
      }
      if (id == range.last && range.last_offset < 0.0) {
        return ClipSelectStatus::PartiallySelected;
      }
      return ClipSelectStatus::Selected;
    }
    return ClipSelectStatus::NotSelected;
  }
};

struct TrackEditResult {
  Vector<Clip> deleted_clips;
  Vector<Clip*> added_clips;
  Vector<Clip*> modified_clips;
  Clip* new_clip;
};

struct MultiEditResult {
  Vector<Pair<uint32_t, Clip>> deleted_clips;
  Vector<Pair<uint32_t, Clip*>> added_clips;
  Vector<Pair<uint32_t, Clip*>> modified_clips;
};

}  // namespace wb
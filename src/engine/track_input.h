#pragma once

#include "core/common.h"
#include "core/list.h"

namespace wb {

struct Track;

enum class TrackInputType {
  None,
  Midi,
  ExternalStereo,
  ExternalMono,
};

struct TrackInput {
  TrackInputType type;
  uint32_t index;

  inline uint32_t as_packed_u32() const {
    return (index & 0xFFFFFFu) | ((static_cast<uint32_t>(type) & 0xFFu) << 24);
  }

  inline static TrackInput from_packed_u32(uint32_t u32) {
    return {
      .type = static_cast<TrackInputType>(u32 >> 24u),
      .index = u32 & 0xFFFFFFu,
    };
  }
};

struct TrackInputAttr : public InplaceList<TrackInputAttr> {
  Track* track;
  bool armed = false;
  bool recording = false;
  uint32_t buffer_id = 0;  // Only valid when recording
  TrackInputAttr(Track* parent_track) : track(parent_track) {
  }
};

struct TrackInputGroup {
  uint32_t input;
  TrackInputAttr* input_attrs;  // First item in track input attribute linked list
};

}  // namespace wb
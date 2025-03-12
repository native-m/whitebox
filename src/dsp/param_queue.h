#pragma once

#include "core/common.h"
#include "core/queue.h"
#include "core/vector.h"

namespace wb::dsp {

struct ParamValue {
  uint32_t sample_offset;
  uint32_t id;
  double value;
};

struct ParamQueue {
  Vector<ParamValue> values;

  inline void clear() {
    values.clear();
  }

  inline uint32_t add_value(uint32_t sample_offset, uint32_t id, double value) {
    size_t dest_idx = values.size();
    for (uint32_t i = 0; i < values.size(); i++) {
      auto& point = values[i];
      if (point.sample_offset == sample_offset) {
        point.value = value;
        return i;
      }
      if (point.sample_offset > sample_offset) {
        dest_idx = i;
        break;
      }
    }

    if (dest_idx == values.size()) {
      values.emplace_back(sample_offset, id, value);
    } else {
      values.emplace_at(dest_idx, sample_offset, id, value);
    }

    return dest_idx;
  }

  inline void push_back_value(uint32_t sample_offset, uint32_t id, double value) {
    assert(values.size() == 0 || sample_offset <= values.back().sample_offset);
    values.emplace_back(sample_offset, id, value);
  }

  inline void transfer_param(ConcurrentRingBuffer<ParamValue>& change) {
    ParamValue value;
    while (change.pop(value)) {
      values.push_back(value);
    }
  }
};

}  // namespace wb::dsp
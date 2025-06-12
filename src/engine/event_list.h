#pragma once

#include "core/vector.h"
#include "event.h"

namespace wb {

struct MidiEventList {
  Vector<MidiEvent> events;

  inline uint32_t size() const {
    return (uint32_t)events.size();
  }
  
  inline MidiEvent& get_event(uint32_t index) {
    return events[index];
  }

  inline const MidiEvent& get_event(uint32_t index) const {
    return events[index];
  }

  inline uint32_t add_event(const MidiEvent& event) {
    size_t dest_idx = events.size();
    for (uint32_t i = 0; i < events.size(); i++) {
      auto& ev = events[i];
      if (ev.buffer_offset == event.buffer_offset) {
        ev = event;
        return i;
      }
      if (ev.buffer_offset > event.buffer_offset) {
        dest_idx = i;
        break;
      }
    }

    if (dest_idx == events.size()) {
      events.emplace_back(event);
    } else {
      events.emplace_at(dest_idx, event);
    }

    return dest_idx;
  }
  
  inline void push_event(const MidiEvent& event) {
    events.push_back(event);
  }
  
  inline void push_event(MidiEvent&& event) {
    events.push_back(event);
  }
  
  inline void clear() {
    events.resize(0);
  }
};

}  // namespace wb
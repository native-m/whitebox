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
  inline void add_event(const MidiEvent& event) {
    events.push_back(event);
  }
  inline void add_event(MidiEvent&& event) {
    events.push_back(event);
  }
  inline void clear() {
    events.resize(0);
  }
};

}  // namespace wb
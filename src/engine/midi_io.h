#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "audio_io_types.h"

namespace wb {

struct MidiDeviceInfo {
  uint32_t id;
  std::string name;
  bool is_input;
  bool is_output;
};

struct MidiEventRaw {
  uint32_t time;
  uint8_t size;
  uint8_t bytes[3];
};

struct MidiIO {
  virtual ~MidiIO() = default;

  virtual bool init() = 0;

  virtual bool rescan() = 0;
  virtual std::vector<MidiDeviceInfo> get_devices() = 0;
  virtual bool connect(uint32_t device_id) = 0;
  virtual void disconnect() = 0;
  virtual bool is_connected() const = 0;
  virtual uint32_t get_connected_device_id() const = 0;

  using MidiCallback = std::function<void(const MidiEventRaw& event)>;
  virtual void set_input_callback(MidiCallback callback) = 0;

  virtual void process(uint32_t n_frames) = 0;

  virtual void send_event(const MidiEventRaw& event) = 0;

  static MidiIO* create(AudioIOType type);
};

} // namespace wb
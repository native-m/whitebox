#pragma once

#include <functional>
#include <string>

#include "command.h"
#include "core/vector.h"

namespace wb {

struct CommandManager {
  using OnHistoryUpdate = std::function<void()>;

  Vector<OnHistoryUpdate> on_history_update_listener;
  InplaceList<Command> commands;
  Command* current_command{};
  Command* last_command{};
  uint32_t max_history = 0;
  uint32_t num_history = 0;
  uint32_t num_histories_used = 0;
  bool is_modified = false;
  bool locked = false;

  void init(uint32_t max_items);
  bool execute(const std::string& name, Command* cmd);
  void undo();
  void redo();
  void reset(bool empty_project = false);
  void signal_history_update_listeners();

  void lock() {
    locked = true;
  }

  void unlock() {
    locked = false;
  }

  template<typename Fn>
  void add_on_history_update_listener(Fn&& fn) {
    on_history_update_listener.push_back(fn);
  }
};

extern CommandManager g_cmd_manager;

}  // namespace wb
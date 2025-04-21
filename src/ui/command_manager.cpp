#include "command_manager.h"

#include "engine/engine.h"
#include "engine/track.h"

namespace wb {
CommandManager g_cmd_manager;

void CommandManager::init(uint32_t max_items) {
  max_history = max_items;
}

void CommandManager::execute(const std::string& name, Command* cmd) {
  if (!cmd->execute()) {
    delete cmd;
    return;
  }

  cmd->name = name;

  if (num_history == max_history) {
    if (Command* cmd = static_cast<Command*>(commands.pop_next_item())) {
      delete cmd;
    }
  }

  if (current_command == nullptr) {
    commands.push_item(cmd);
  } else {
    while (Command* cmd = static_cast<Command*>(current_command->pop_next_item())) {
      delete cmd;
    }
    current_command->push_item(cmd);
  }

  current_command = cmd;
  last_command = cmd;
  is_modified = true;

  if (num_history < max_history) {
    ++num_history;
  }
}

void CommandManager::undo() {
  if (num_history == 0)
    return;
  current_command->undo();
  current_command = current_command->prev();
  is_modified = true;
  --num_history;
  signal_history_update_listeners();
}

void CommandManager::redo() {
  if (num_history == max_history || current_command == last_command)
    return;
  current_command = current_command->next();
  current_command->execute();
  is_modified = true;
  ++num_history;
  signal_history_update_listeners();
}

void CommandManager::reset(bool empty_project) {
  if (empty_project) {
    is_modified = false;
  }
  while (Command* cmd = static_cast<Command*>(commands.pop_next_item())) {
    delete cmd;
  }
  num_history = 0;
  current_command = nullptr;
  last_command = nullptr;
}

void CommandManager::signal_history_update_listeners() {
  for (auto& listener : on_history_update_listener) {
    listener();
  }
}

}  // namespace wb
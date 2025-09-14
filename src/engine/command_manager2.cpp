#include "command_manager2.h"

#include "core/vector.h"

namespace wb {

static Vector<Pair<CmdHistoryUpdateCallbackFn, void*>> history_update_listener;
static uint32_t max_commands_;
static uint32_t num_commands_;
static uint32_t num_commands_used_;
static bool locked_;

InplaceList<Command2> CommandManager2::commands;
Command2* CommandManager2::current_command;
Command2* CommandManager2::last_command;

static void call_history_update_listener();

void CommandManager2::initialize(uint32_t num_commands) {
  max_commands_ = num_commands;
}

bool CommandManager2::execute_command(std::string_view name, Command2* cmd) {
  if (!cmd->execute()) {
    delete cmd;
    return false;
  }

  cmd->cmd_name = name;

  if (num_commands_ == max_commands_) {
    if (Command2* cmd = static_cast<Command2*>(commands.pop_next_item())) {
      delete cmd;
    }
  }

  if (current_command == nullptr) {
    commands.push_item(cmd);
  } else {
    while (Command2* cmd = static_cast<Command2*>(current_command->pop_next_item())) {
      delete cmd;
    }
    current_command->push_item(cmd);
  }

  current_command = cmd;
  last_command = cmd;
  // is_modified = true;

  if (num_commands_ < max_commands_) {
    ++num_commands_;
  }

  return true;
}

void CommandManager2::undo() {
  if (locked_ || num_commands_ == 0)
    return;
  current_command->undo();
  current_command = current_command->prev();
  // is_modified = true;
  --num_commands_;
  call_history_update_listener();
}

void CommandManager2::redo() {
  if (locked_ || num_commands_ == max_commands_ || current_command == last_command)
    return;
  current_command = current_command->next();
  current_command->execute();
  // is_modified = true;
  ++num_commands_;
  call_history_update_listener();
}

void CommandManager2::flush() {
  while (Command2* cmd = static_cast<Command2*>(commands.pop_next_item())) {
    delete cmd;
  }
  current_command = nullptr;
  last_command = nullptr;
  num_commands_ = 0;
}

void CommandManager2::lock() {
  locked_ = true;
}

void CommandManager2::unlock() {
  locked_ = false;
}

uint32_t CommandManager2::get_executed_commands_count() {
  return num_commands_;
}

void CommandManager2::add_cmd_history_update_listener(void* userdata, CmdHistoryUpdateCallbackFn fn) {
  history_update_listener.emplace_back(fn, userdata);
}

void call_history_update_listener() {
  for (auto [fn, userdata] : history_update_listener) {
    fn(userdata);
  }
}

}  // namespace wb
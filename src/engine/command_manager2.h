#pragma once

#include "command2.h"

namespace wb {

using CmdHistoryUpdateCallbackFn = void (*)(void* userdata);

struct CommandManager2 {
  static InplaceList<Command2> commands;
  static Command2* current_command;
  static Command2* last_command;

  static void initialize(uint32_t num_commands);
  static bool execute_command(std::string_view name, Command2* cmd);
  static void undo();
  static void redo();
  static void flush();
  static void lock();
  static void unlock();
  static uint32_t get_executed_commands_count();
  static void add_cmd_history_update_listener(void* userdata, CmdHistoryUpdateCallbackFn fn);
};

}  // namespace wb
#pragma once

#include <string_view>

#include "core/color.h"
#include "core/common.h"
#include "core/list.h"

namespace wb {

struct Command2 : public InplaceList<Command2> {
  std::string_view cmd_name;
  virtual ~Command2() {
  }
  virtual bool execute() = 0;
  virtual void undo() = 0;
};

}  // namespace wb
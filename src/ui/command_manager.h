#pragma once

#include "command.h"
#include "core/vector.h"
#include <string>
#include <variant>

namespace wb {

struct HistoryItem {
    std::string name;
    std::variant<ClipMoveCmd, ClipShiftCmd, ClipResizeCmd> data;

    template <typename T>
    void set(const std::string& new_name, T&& new_data) {
        name = new_name;
        data.emplace(std::forward(new_data));
    }
};

struct CommandManager {
    Vector<HistoryItem> items;
    uint32_t max_history = 0;
    uint32_t start_pos = 0;
    uint32_t pos = 0;

    void init(uint32_t max_items);
    void undo();

    template <typename T>
    void execute(const std::string& name, T&& cmd) {
        HistoryItem& item = items.size() == max_history ? items[pos] : items.emplace_back();
        uint32_t new_pos = pos + 1;
        item.set(name, std::forward(cmd));
        std::get<T>(item.data).execute();
        pos = new_pos % max_history;
    }
};

extern CommandManager g_cmd_manager;

} // namespace wb
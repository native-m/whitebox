#pragma once

#include "command.h"
#include "core/vector.h"
#include <string>
#include <variant>
#include <functional>

namespace wb {

struct EmptyCmd {
    void execute() {}
    void undo() {}
};

struct HistoryItem {
    std::string name;
    std::variant<EmptyCmd, ClipMoveCmd, ClipShiftCmd, ClipResizeCmd> data;

    template <typename T>
    void set(const std::string& new_name, T&& new_data) {
        name = new_name;
        data = std::move(new_data);
    }
};

struct CommandManager {
    using OnHistoryUpdate = std::function<void()>;

    Vector<OnHistoryUpdate> on_history_update_listener;
    Vector<HistoryItem> items;
    uint32_t max_history = 0;
    uint32_t size = 0;
    uint32_t pos = 0;

    void init(uint32_t max_items);
    void undo();
    void redo();
    void clear_history();
    void signal_all_update_listeners();

    template <typename T>
    void execute(const std::string& name, T&& cmd) {
        HistoryItem& item = items[pos];
        uint32_t new_pos = pos + 1;
        item.set(name, std::move(cmd));
        std::get<std::decay_t<T>>(item.data).execute();
        pos = new_pos % max_history;
        if (pos > size && size < max_history) {
            size++;
        }
        signal_all_update_listeners();
    }

    template <typename Fn>
    void add_on_history_update_listener(Fn&& fn) {
        on_history_update_listener.push_back(fn);
    }
};

extern CommandManager g_cmd_manager;

} // namespace wb
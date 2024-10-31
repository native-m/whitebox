#pragma once

#include "command.h"
#include "core/vector.h"
#include <functional>
#include <string>
#include <variant>

namespace wb {

template <typename T>
concept CommandType = requires(T cmd) {
    cmd.execute();
    cmd.undo();
};

struct EmptyCmd {
    void execute() {}
    void undo() {}
};

struct HistoryItem {
    std::string name;
    std::variant<EmptyCmd, ClipAddFromFileCmd, ClipMoveCmd, ClipShiftCmd, ClipResizeCmd, ClipDuplicateCmd,
                 ClipDeleteCmd, ClipDeleteRegionCmd>
        data;

    template <CommandType T>
    void set(const std::string& new_name, T&& new_data) {
        name = new_name;
        data = std::move(new_data);
    }

    void unset() {
        name.clear();
        data = EmptyCmd {};
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
    void reset();
    void signal_all_update_listeners();

    template <CommandType T>
    void execute(const std::string& name, T&& cmd) {
        HistoryItem& item = items[pos];
        uint32_t new_pos = pos + 1;
        item.set(name, std::move(cmd));
        std::get<std::decay_t<T>>(item.data).execute();
        if (new_pos > size && size <= max_history) {
            size++;
        }
        pos = new_pos % max_history;
        signal_all_update_listeners();
    }

    template <typename Fn>
    void add_on_history_update_listener(Fn&& fn) {
        on_history_update_listener.push_back(fn);
    }
};

extern CommandManager g_cmd_manager;

} // namespace wb
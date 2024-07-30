#include "command_manager.h"
#include "engine/engine.h"
#include "engine/track.h"

namespace wb {
CommandManager g_cmd_manager;

void CommandManager::init(uint32_t max_items) {
    items.resize(max_items);
    max_history = max_items;
}

void CommandManager::undo() {
    if (pos == 0)
        return;
    HistoryItem& item = items[--pos];
    std::visit([](auto&& data) { data.undo(); }, item.data);
    signal_all_update_listeners();
}

void CommandManager::signal_all_update_listeners() {
    for (auto& listener : on_history_update_listener) {
        listener();
    }
}

} // namespace wb
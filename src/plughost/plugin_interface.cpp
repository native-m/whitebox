#include "plugin_interface.h"
#include "extern/xxhash.h"

namespace wb {
PluginInterface::PluginInterface(uint64_t module_hash, PluginFormat format) : module_hash(module_hash), format(format) {
}

size_t PluginUIDHash::operator()(PluginUID uid) const {
    return XXH64(uid, sizeof(PluginUID), 69420);
}
} // namespace wb
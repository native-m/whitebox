#include "plugin_interface.h"
#include "extern/xxhash.h"

namespace wb {
PluginInterface::PluginInterface(uint64_t module_hash, PluginFormat format) : module_hash(module_hash), format(format) {
}
} // namespace wb
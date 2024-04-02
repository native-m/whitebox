#include "debug.h"

namespace wb {

std::optional<Log> Log::g_main_logger = std::make_optional<Log>("wb");

}

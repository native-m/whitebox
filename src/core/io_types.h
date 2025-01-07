#pragma once

namespace wb {
struct IOOpenMode {
    enum {
        Read = 1,
        Write = 2,
        Truncate = 4,
    };
};

enum class IOSeekMode {
    Begin,
    Relative,
    End,
};
} // namespace wb
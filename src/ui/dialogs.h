#pragma once

#include "core/common.h"

namespace wb {
struct ConfirmDialog {
    enum {
        None = 0,
        Yes = 1 << 0,
        No = 1 << 1,
        Ok = 1 << 2,
        Cancel = 1 << 3,

        // Template flags for commonly used buttons
        YesNo = Yes | No,
        YesNoCancel = Yes | No | Cancel,
        OkCancel = Ok | Cancel,
    };
};
using ConfirmDialogFlags = uint32_t;

ConfirmDialogFlags confirm_dialog(const char* str, const char* msg, ConfirmDialogFlags flags);
void export_audio_dialog();
} // namespace wb
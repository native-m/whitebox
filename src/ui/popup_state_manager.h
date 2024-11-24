#pragma once

#include "core/common.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <string_view>
#include <unordered_map>

namespace wb {

struct PopupState {
    ImGuiWindow* window_;
    bool popup_open;

    PopupState(ImGuiWindow* window, bool popup_open) : window_(window), popup_open(popup_open) {}

    ~PopupState() {
        if (window_ && !popup_open)
            window_->StateStorage.Clear();
    }

    inline bool is_popup_open() const { return popup_open; }

    inline int GetInt(ImGuiID key, int default_val = 0) const { return window_->StateStorage.GetInt(key, default_val); }
    inline void SetInt(ImGuiID key, int val) { return window_->StateStorage.SetInt(key, val); }

    inline bool GetBool(ImGuiID key, bool default_val = false) const {
        return window_->StateStorage.GetBool(key, default_val);
    };
    inline void SetBool(ImGuiID key, bool val) { return window_->StateStorage.SetBool(key, val); }

    inline float GetFloat(ImGuiID key, float default_val = 0.0f) const {
        return window_->StateStorage.GetFloat(key, default_val);
    };
    inline void SetFloat(ImGuiID key, float val) { return window_->StateStorage.SetFloat(key, val); }

    inline void* GetVoidPtr(ImGuiID key) const { return window_->StateStorage.GetVoidPtr(key); };
    inline void SetVoidPtr(ImGuiID key, void* val) { return window_->StateStorage.SetVoidPtr(key, val); }

    int* GetIntRef(ImGuiID key, int default_val = 0) { return window_->StateStorage.GetIntRef(key, default_val); }
    bool* GetBoolRef(ImGuiID key, bool default_val = false) {
        return window_->StateStorage.GetBoolRef(key, default_val);
    }
    float* GetFloatRef(ImGuiID key, float default_val = 0.0f) {
        return window_->StateStorage.GetFloatRef(key, default_val);
    }
    void** GetVoidPtrRef(ImGuiID key, void* default_val = NULL) {
        return window_->StateStorage.GetVoidPtrRef(key, default_val);
    }
};
} // namespace wb
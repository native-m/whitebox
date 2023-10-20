#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace wb
{

    struct PopupStateContext
    {
        ImGuiWindow* window;
        bool clear_after_close;

        PopupStateContext(bool clear_after_close = true) :
            window(ImGui::GetCurrentContext()->CurrentWindow),
            clear_after_close(clear_after_close)
        {
            assert(window->Flags & ImGuiWindowFlags_Popup);
        }

        ~PopupStateContext()
        {
            if (clear_after_close && !ImGui::IsPopupOpen(window->PopupId, 0))
                window->StateStorage.Clear();
        }

        inline int GetInt(ImGuiID key, int default_val = 0) const { return window->StateStorage.GetInt(key, default_val); }
        inline void SetInt(ImGuiID key, int val) { return window->StateStorage.SetInt(key, val); }
        inline bool GetBool(ImGuiID key, bool default_val = false) const { return window->StateStorage.GetBool(key, default_val); };
        inline void SetBool(ImGuiID key, bool val) { return window->StateStorage.SetBool(key, val); }
        inline float GetFloat(ImGuiID key, float default_val = 0.0f) const { return window->StateStorage.GetFloat(key, default_val); };
        inline void SetFloat(ImGuiID key, float val) { return window->StateStorage.SetFloat(key, val); }
        inline void* GetVoidPtr(ImGuiID key) const { return window->StateStorage.GetVoidPtr(key); };
        inline void SetVoidPtr(ImGuiID key, void* val) { return window->StateStorage.SetVoidPtr(key, val); }

        int* GetIntRef(ImGuiID key, int default_val = 0) { return window->StateStorage.GetIntRef(key, default_val); }
        bool* GetBoolRef(ImGuiID key, bool default_val = false) { return window->StateStorage.GetBoolRef(key, default_val); }
        float* GetFloatRef(ImGuiID key, float default_val = 0.0f) { return window->StateStorage.GetFloatRef(key, default_val); }
        void** GetVoidPtrRef(ImGuiID key, void* default_val = NULL) { return window->StateStorage.GetVoidPtrRef(key, default_val); }
    };
}
#include "window_manager.h"

#ifdef WB_PLATFORM_WINDOWS
#include <dwmapi.h>
#include <imgui.h>
#include <shobjidl_core.h>

#define DWM_ATTRIBUTE_USE_IMMERSIVE_DARK_MODE 20
#define DWM_ATTRIBUTE_CAPTION_COLOR           35

#include "core/debug.h"

namespace wb {

static ITaskbarList4* taskbar_list;

void init_platform_window_manager() {
  enum class PreferredAppMode { Default, AllowDark, ForceDark, ForceLight, Max };
  using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode appMode);
  HMODULE uxtheme = LoadLibraryEx(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (uxtheme) {
    SetPreferredAppModeFn set_preferred_app_mode_fn = (SetPreferredAppModeFn)GetProcAddress(uxtheme, MAKEINTRESOURCEA(135));
    if (set_preferred_app_mode_fn)
      set_preferred_app_mode_fn(PreferredAppMode::ForceDark);
  }

  HRESULT hr = ::CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&taskbar_list));
  if (FAILED(hr)) {
    Log::warn("Cannot create ITaskbarList4 instance");
    taskbar_list = nullptr;
  }
}

void shutdown_platform_window_manager() {
  taskbar_list->Release();
}

WindowNativeHandle wm_get_native_window_handle(SDL_Window* window) {
  SDL_PropertiesID props = SDL_GetWindowProperties(window);
  HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
  if (hwnd) {
    return {
      hwnd,
      nullptr,
    };
  }
  return {};
}

void wm_enable_taskbar_progress_indicator(bool enable) {
  if (taskbar_list) {
    SDL_Window* main_window = wm_get_main_window();
    HWND hwnd =
        (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(main_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!enable)
      taskbar_list->SetProgressValue(hwnd, 0, 100);
    taskbar_list->SetProgressState(hwnd, enable ? TBPF_NORMAL : TBPF_NOPROGRESS);
  }
  // TODO(native-m): Replace this with SDL_SetWindowProgressState function
  // SDL_SetWindowProgressState(main_window, enable ? SDL_PROGRESS_STATE_NORMAL : SDL_PROGRESS_STATE_NONE);
}

void wm_set_taskbar_progress_value(float progress) {
  if (taskbar_list) {
    SDL_Window* main_window = wm_get_main_window();
    HWND hwnd =
        (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(main_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    taskbar_list->SetProgressValue(hwnd, (uint32_t)(progress * 100.0f), 100);
  }
  // TODO(native-m): Replace this with SDL function
  // SDL_SetWindowProgressValue(main_window, progress);
}

void wm_set_dark_mode(SDL_Window* window) {
  SDL_PropertiesID props = SDL_GetWindowProperties(window);
  ImU32 title_bar_color = ImColor(0.15f, 0.15f, 0.15f, 1.00f) & 0x00FFFFFF;
#ifdef WB_PLATFORM_WINDOWS
  HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
  BOOL dark_mode = true;
  ::DwmSetWindowAttribute(hwnd, DWM_ATTRIBUTE_USE_IMMERSIVE_DARK_MODE, &dark_mode, sizeof(dark_mode));
  ::DwmSetWindowAttribute(hwnd, DWM_ATTRIBUTE_CAPTION_COLOR, &title_bar_color, sizeof(title_bar_color));
#endif
}

}  // namespace wb

#endif

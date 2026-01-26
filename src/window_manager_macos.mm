#include "SDL3/SDL_video.h"
#include "window_manager.h"

#ifdef WB_PLATFORM_MACOS

#include <AppKit/NSWindow.h>
#include <imgui.h>

namespace wb {

void init_platform_window_manager() {
}

void shutdown_platform_window_manager() {
}

WindowNativeHandle wm_get_native_window_handle(SDL_Window* window) {
  SDL_PropertiesID props = SDL_GetWindowProperties(window);
  NSWindow* native_window = (NSWindow*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
  return { .type = WMType::MacOS, .window = native_window };
}

void wm_enable_taskbar_progress_indicator(bool enable) {
  // TODO(native-m): Replace this with SDL_SetWindowProgressState function
  // SDL_SetWindowProgressState(main_window, enable ? SDL_PROGRESS_STATE_NORMAL : SDL_PROGRESS_STATE_NONE);
}

void wm_set_taskbar_progress_value(float progress) {
  // TODO(native-m): Replace this with SDL function
  // SDL_SetWindowProgressValue(main_window, progress);
}

void wm_set_dark_mode(SDL_Window* window) {
  if (window == wm_get_main_window()) {
    NSWindowStyleMask style = NSWindowStyleMaskFullSizeContentView | NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                              NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable | NSWindowStyleMaskBorderless;
    NSWindow* ns_window = (NSWindow*)(wm_get_native_window_handle(window).window);
    [ns_window setStyleMask:style];
    [ns_window setTitlebarAppearsTransparent:true];
  }
}

}  // namespace wb

#endif
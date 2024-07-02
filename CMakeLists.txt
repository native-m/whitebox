# CMakeList.txt : CMake project for whitebox, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.20)

# Enable Hot Reload for MSVC compilers if supported.
if (MSVC AND POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

#set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project("whitebox")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED 20)
option(WB_VST3_SDK_PATH "Path to VST3 SDK")

include(CheckIPOSupported)
check_ipo_supported(RESULT WB_IPO_SUPPORTED OUTPUT output)

if (WB_IPO_SUPPORTED)
    message("WB: IPO is supported by the toolchain!")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

include(Dependencies.cmake)

enable_testing()
add_subdirectory(assets)

set(WB_SOURCES
    "src/core/algorithm.h"
    "src/core/audio_buffer.h"
    "src/core/audio_format_conv.h"
    "src/core/audio_format.h"
    "src/core/bit_manipulation.h"
    "src/core/color.h"
    "src/core/common.h"
    "src/core/debug.cpp"
    "src/core/debug.h"
    "src/core/defer.h"
    "src/core/json.hpp"
    "src/core/json_fwd.hpp"
    "src/core/math.h"
    "src/core/memory.h"
    "src/core/midi.cpp"
    "src/core/midi.h"
    "src/core/platform_def.h"
    "src/core/queue.h"
    "src/core/thread.cpp"
    "src/core/thread.h"
    "src/core/types.h"
    "src/core/vector.h"
    
    "src/engine/assets_table.cpp"
    "src/engine/assets_table.h"
    "src/engine/audio_io.cpp"
    "src/engine/audio_io.h"
    "src/engine/audio_io_wasapi.cpp"
    "src/engine/audio_io_pulseaudio.cpp"
    "src/engine/clip.h"
    "src/engine/engine.cpp"
    "src/engine/engine.h"
    "src/engine/event.h"
    "src/engine/param_changes.h"
    "src/engine/project.cpp"
    "src/engine/project.h"
    "src/engine/sample.cpp"
    "src/engine/sample.h"
    "src/engine/sample_peaks.h"
    "src/engine/track.cpp"
    "src/engine/track.h"
    "src/engine/vu_meter.h"

    "src/plughost/vst3host.cpp"
    "src/plughost/vst3host.h"
    "src/plughost/wb_plug.h"

    "src/ui/browser.cpp"
    "src/ui/browser.h"
    "src/ui/controls.cpp"
    "src/ui/controls.h"
    "src/ui/draw.cpp"
    "src/ui/draw.h"
    "src/ui/file_dialog.cpp"
    "src/ui/file_dialog.h"
    "src/ui/file_dropper.cpp"
    "src/ui/file_dropper.h"
    "src/ui/font.cpp"
    "src/ui/font.h"
    "src/ui/forms.h"
    "src/ui/IconsMaterialSymbols.h"
    "src/ui/layout.h"
    "src/ui/mixer.cpp"
    "src/ui/mixer.h"
    "src/ui/piano_roll.cpp"
    "src/ui/piano_roll.h"
    "src/ui/popup_state_manager.h"
    "src/ui/settings.cpp"
    "src/ui/settings.h"
    "src/ui/test_controls.cpp"
    "src/ui/timeline.cpp"
    "src/ui/timeline.h"

    "src/app.cpp"
    "src/app.h"
    "src/app_sdl2.cpp"
    "src/app_sdl2.h"
    "src/renderer.cpp"
    "src/renderer.h"
    "src/renderer_d3d11.cpp"
    "src/renderer_d3d11.h"
    "src/renderer_vulkan.cpp"
    "src/renderer_vulkan.h"
    "src/settings_data.cpp"
    "src/settings_data.h"
    "src/vk_stub.cpp"
    "src/vk_stub.h"
)

set(WB_LINK_LIBRARIES
    SDL2-static
    spdlog::spdlog
    Vulkan::Headers
    VulkanMemoryAllocator
    volk_headers
    vk-bootstrap
    imgui-sdl2
    imgui-cpp
    imgui-freetype
    imgui-vulkan
    sndfile
    midi-parser
    nfd
    vst3-sdk-hosting)

if (WIN32)
    set(WB_LINK_LIBRARIES ${WB_LINK_LIBRARIES} imgui-d3d11 Avrt Dwmapi)
else ()
    set(WB_LINK_LIBRARIES ${WB_LINK_LIBRARIES} xcb X11 X11-xcb)
endif()

add_library(whitebox-lib STATIC ${WB_SOURCES})
target_link_libraries(whitebox-lib PUBLIC ${WB_LINK_LIBRARIES})
target_include_directories(whitebox-lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(whitebox-lib PROPERTIES CXX_STANDARD 20)

if (UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBPULSE REQUIRED IMPORTED_TARGET libpulse)
    target_link_libraries(whitebox-lib PRIVATE PkgConfig::LIBPULSE)
endif ()

if (WIN32)
    target_compile_definitions(whitebox-lib PUBLIC -DUNICODE -D_UNICODE)
endif()

add_executable(whitebox "src/main.cpp")
target_link_libraries(whitebox whitebox-lib)
add_dependencies(whitebox wb-assets)
set_target_properties(whitebox PROPERTIES CXX_STANDARD 20)


#set_property(TARGET whitebox-lib whitebox PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(test)

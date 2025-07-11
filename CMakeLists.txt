# CMakeList.txt : CMake project for whitebox, include source and define
# project specific logic here.
#
cmake_minimum_required(VERSION 3.20)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project("whitebox" VERSION 0.1.0)

include(CMakeDependentOption)
option(WB_BUILD_TEST "Build test programs" ON)
option(WB_ENABLE_ASAN "Enable address sanitizer" OFF)
option(WB_ENABLE_INSTALL "Enable install" OFF)
cmake_dependent_option(WB_ENABLE_PACKAGING "Enable packaging with CPack" OFF "WB_ENABLE_INSTALL" OFF)

if (WB_ENABLE_ASAN AND (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"))
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fsanitize=address")
elseif (WB_ENABLE_ASAN AND (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
endif()

if (WIN32)
    set(WB_PLATFORM_WINDOWS 1)
elseif (LINUX)
    set(WB_PLATFORM_LINUX 1)
elseif (APPLE AND (NOT IOS))
    set(WB_PLATFORM_MACOS 1)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckIPOSupported)
check_ipo_supported(RESULT WB_IPO_SUPPORTED OUTPUT output)

if (WB_BUILD_TEST)
    enable_testing()
endif()

if (WB_ENABLE_INSTALL)
    if (CMAKE_BUILD_TYPE MATCHES "[Dd]ebug")
        set(CMAKE_INSTALL_DEBUG_LIBRARIES ${WB_INSTALL_DEBUG_SYSTEM_LIBARIES})
        set(CMAKE_INSTALL_DEBUG_LIBRARIES_ONLY ${WB_INSTALL_DEBUG_SYSTEM_LIBARIES})
    endif()
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
endif()

include(Dependencies.cmake)
add_subdirectory(assets)

set(WB_SOURCES
    "src/core/algorithm.h"
    "src/core/audio_buffer.h"
    "src/core/audio_format_conv.cpp"
    "src/core/audio_format_conv.h"
    "src/core/audio_format.h"
    "src/core/bitset.h"
    "src/core/bit_manipulation.h"
    "src/core/byte_buffer.h"
    "src/core/color.cpp"
    "src/core/color.h"
    "src/core/common.h"
    "src/core/core_math.h"
    "src/core/debug.cpp"
    "src/core/debug.h"
    "src/core/defer.h"
    "src/core/deferred_job.cpp"
    "src/core/deferred_job.h"
    "src/core/fs.cpp"
    "src/core/fs.h"
    "src/core/fs_win32.cpp"
    "src/core/fs_stdc.cpp"
    "src/core/intrinsics.h"
    "src/core/io_types.h"
    "src/core/list.h"
    "src/core/memory.cpp"
    "src/core/memory.h"
    "src/core/mem_info.cpp"
    "src/core/mem_info.h"
    "src/core/midi.cpp"
    "src/core/midi.h"
    "src/core/midi_file.cpp"
    "src/core/midi_file.h"
    "src/core/panning_law.cpp"
    "src/core/panning_law.h"
    "src/core/platform_def.h"
    "src/core/queue.h"
    "src/core/serdes.h"
    "src/core/span.h"
    "src/core/stream.h"
    "src/core/thread.cpp"
    "src/core/thread.h"
    "src/core/timing.cpp"
    "src/core/timing.h"
    "src/core/types.h"
    "src/core/vector.h"

    "src/dsp/codec.cpp"
    "src/dsp/codec.h"
    "src/dsp/dsp_ops.h"
    "src/dsp/param_queue.h"
    "src/dsp/sample.cpp"
    "src/dsp/sample.h"
    "src/dsp/sampler.cpp"
    "src/dsp/sampler.h"

    "src/engine/assets_table.cpp"
    "src/engine/assets_table.h"
    "src/engine/audio_io.cpp"
    "src/engine/audio_io.h"
    "src/engine/audio_io_wasapi.cpp"
    "src/engine/audio_io_pulseaudio.cpp"
    "src/engine/audio_record.cpp"
    "src/engine/audio_record.h"
    "src/engine/clip.h"
    "src/engine/clip_edit.h"
    "src/engine/engine.cpp"
    "src/engine/engine.h"
    "src/engine/etypes.h"
    "src/engine/envelope_storage.h"
    "src/engine/event.h"
    "src/engine/event_list.h"
    "src/engine/export_prop.h"
    "src/engine/midi_data.cpp"
    "src/engine/midi_data.h"
    "src/engine/midi_voice.cpp"
    "src/engine/midi_voice.h"
    "src/engine/param_changes.h"
    "src/engine/project.cpp"
    "src/engine/project.h"
    "src/engine/test_synth.cpp"
    "src/engine/test_synth.h"
    "src/engine/track.cpp"
    "src/engine/track.h"
    "src/engine/track_input.h"
    "src/engine/vu_meter.h"

    "src/extern/dr_mp3.h"
    "src/extern/dr_mp3_stub.c"
    "src/extern/json.hpp"
    "src/extern/json_fwd.hpp"
    "src/extern/sdl_wm.h"
    "src/extern/xxhash.c"
    "src/extern/xxhash.h"

    "src/gfx/draw.cpp"
    "src/gfx/draw.h"
    "src/gfx/renderer.cpp"
    "src/gfx/renderer.h"
    "src/gfx/renderer_vulkan.cpp"
    "src/gfx/renderer_vulkan.h"
    "src/gfx/vk_stub.cpp"
    "src/gfx/vk_stub.h"
    "src/gfx/vsync_provider.cpp"
    "src/gfx/vsync_provider.h"
    "src/gfx/waveform_visual.cpp"
    "src/gfx/waveform_visual.h"

    "src/plughost/plugin_interface.cpp"
    "src/plughost/plugin_interface.h"
    "src/plughost/plugin_manager.cpp"
    "src/plughost/plugin_manager.h"
    "src/plughost/vst3host.cpp"
    "src/plughost/vst3host.h"

    "src/ui/browser.cpp"
    "src/ui/browser.h"
    "src/ui/clip_editor.cpp"
    "src/ui/clip_editor.h"
    "src/ui/command.cpp"
    "src/ui/command.h"
    "src/ui/command_manager.cpp"
    "src/ui/command_manager.h"
    "src/ui/context_menu.cpp"
    "src/ui/context_menu.h"
    "src/ui/controls.cpp"
    "src/ui/controls.h"
    "src/ui/control_bar.h"
    "src/ui/control_bar.cpp"
    "src/ui/dialogs.cpp"
    "src/ui/dialogs.h"
    "src/ui/env_editor.cpp"
    "src/ui/env_editor.h"
    "src/ui/export_audio_dlg.cpp"
    "src/ui/file_dialog.cpp"
    "src/ui/file_dialog.h"
    "src/ui/file_dropper.cpp"
    "src/ui/file_dropper.h"
    "src/ui/font.cpp"
    "src/ui/font.h"
    "src/ui/forms.h"
    "src/ui/grid.h"
    "src/ui/grid.cpp"
    "src/ui/hotkeys.h"
    "src/ui/hotkeys.cpp"
    "src/ui/IconsMaterialSymbols.h"
    "src/ui/mixer.cpp"
    "src/ui/mixer.h"
    "src/ui/plugins.cpp"
    "src/ui/plugins.h"
    "src/ui/plugin_mgr.cpp"
    "src/ui/plugin_mgr.h"
    "src/ui/popup_state_manager.h"
    "src/ui/settings.cpp"
    "src/ui/settings.h"
    "src/ui/test_controls.cpp"
    "src/ui/timeline.cpp"
    "src/ui/timeline.h"
    "src/ui/timeline_base.cpp"
    "src/ui/timeline_base.h"
    "src/ui/window.h"
    "src/ui/window.cpp"

    "src/app.cpp"
    "src/app.h"
    "src/app_event.cpp"
    "src/app_event.h"
    "src/config.cpp"
    "src/config.h"
    "src/path_def.cpp"
    "src/path_def.h"
    "src/window_manager.cpp"
    "src/window_manager.h"
)

set(WB_LINK_LIBRARIES
    SDL3::SDL3-static
    spdlog::spdlog
    Vulkan::Headers
    VulkanMemoryAllocator
    volk_headers
    imgui-sdl3
    imgui-cpp
    imgui-freetype
    imgui-vulkan
    leveldb
    msgpack-cxx
    sndfile
    vorbisfile
    midi-parser
    nfd
    vst3-sdk-hosting)

if (WB_PLATFORM_WINDOWS)
    set(WB_LINK_LIBRARIES ${WB_LINK_LIBRARIES} imgui-d3d11 Avrt Dwmapi)
elseif (WB_PLATFORM_LINUX)
    set(WB_LINK_LIBRARIES ${WB_LINK_LIBRARIES} PkgConfig::LIBPULSE xcb X11 X11-xcb)
endif ()

add_library(whitebox-lib STATIC ${WB_SOURCES})
target_link_libraries(whitebox-lib PUBLIC ${WB_LINK_LIBRARIES})
target_include_directories(whitebox-lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(whitebox-lib PROPERTIES
    CXX_STANDARD 20
    DEBUG_POSTFIX "-debug")

target_precompile_headers(whitebox-lib PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/core/core_math.h>")

if (WB_IPO_SUPPORTED)
    set_target_properties(whitebox-lib PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

if (WB_PLATFORM_WINDOWS)
    target_compile_definitions(whitebox-lib PUBLIC -DUNICODE -D_UNICODE)
endif()

add_executable(whitebox "src/main.cpp")
target_link_libraries(whitebox whitebox-lib)
add_dependencies(whitebox wb-assets)
set_target_properties(whitebox PROPERTIES
    CXX_STANDARD 20
    DEBUG_POSTFIX "-debug")

if (WB_IPO_SUPPORTED)
    set_target_properties(whitebox PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

if (WB_BUILD_TEST)
    add_subdirectory(test)
endif()

if (WB_ENABLE_INSTALL)
    install(TARGETS whitebox
        RUNTIME DESTINATION "."
        COMPONENT whitebox)
    install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
        DESTINATION "."
        COMPONENT whitebox)
    install(FILES ${WB_ASSET_INSTALL_FILES}
        DESTINATION assets
        COMPONENT whitebox)
endif ()

if (WB_ENABLE_PACKAGING)
    set(CPACK_PACKAGE_NAME "whitebox")
    set(CPACK_PACKAGE_VENDOR "Whitebox Developers")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Whitebox DAW"
        CACHE STRING "Free & open-source digital audio workstation"
    )
    set(CPACK_COMPONENT_UNSPECIFIED_REQUIRED FALSE)
    set(CPACK_COMPONENT_UNSPECIFIED_DISABLED TRUE)
    set(CPACK_COMPONENTS_ALL whitebox)
    set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
    set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
    set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
    include(CPack)
endif ()

#set_property(TARGET whitebox-lib whitebox PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

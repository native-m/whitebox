# CMakeList.txt : CMake project for whitebox, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.20)

# Enable Hot Reload for MSVC compilers if supported.
if (MSVC AND POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project("whitebox" VERSION 0.1.0)
include(CMakeDependentOption)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED 20)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WB_BUILD_TEST "Build test programs" ON)
option(WB_ENABLE_INSTALL "Enable install" OFF)
cmake_dependent_option(WB_ENABLE_PACKAGING "Enable packaging with CPack" OFF "WB_ENABLE_INSTALL" OFF)

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
    "src/core/audio_format_conv.h"
    "src/core/audio_format.h"
    "src/core/bit_manipulation.h"
    "src/core/color.h"
    "src/core/common.h"
    "src/core/debug.cpp"
    "src/core/debug.h"
    "src/core/defer.h"
    "src/core/math.h"
    "src/core/memory.h"
    "src/core/midi.cpp"
    "src/core/midi.h"
    "src/core/platform_def.h"
    "src/core/queue.h"
    "src/core/thread.cpp"
    "src/core/thread.h"
    "src/core/tracked_resource.h"
    "src/core/types.h"
    "src/core/vector.h"
    
    "src/engine/assets_table.cpp"
    "src/engine/assets_table.h"
    "src/engine/audio_io.cpp"
    "src/engine/audio_io.h"
    "src/engine/audio_io_wasapi.cpp"
    "src/engine/audio_io_pulseaudio.cpp"
    "src/engine/clip.h"
    "src/engine/clip_edit.h"
    "src/engine/engine.cpp"
    "src/engine/engine.h"
    "src/engine/envelope_storage.h"
    "src/engine/event.h"
    "src/engine/event_list.h"
    "src/engine/param_changes.h"
    "src/engine/project.cpp"
    "src/engine/project.h"
    "src/engine/sample.cpp"
    "src/engine/sample.h"
    "src/engine/sample_peaks.h"
    "src/engine/track.cpp"
    "src/engine/track.h"
    "src/engine/vu_meter.h"

    "src/extern/dr_mp3.h"
    "src/extern/dr_mp3_stub.c"
    "src/extern/json.hpp"
    "src/extern/json_fwd.hpp"

    "src/gfx/draw.cpp"
    "src/gfx/draw.h"
    "src/gfx/renderer.cpp"
    "src/gfx/renderer.h"
    "src/gfx/renderer_d3d11.cpp"
    "src/gfx/renderer_d3d11.h"
    "src/gfx/renderer_vulkan.cpp"
    "src/gfx/renderer_vulkan.h"
    "src/gfx/vsync_provider.cpp"
    "src/gfx/vsync_provider.h"
    "src/gfx/vk_stub.cpp"
    "src/gfx/vk_stub.h"

    "src/plughost/vst3host.cpp"
    "src/plughost/vst3host.h"
    "src/plughost/wb_plug.h"

    "src/ui/browser.cpp"
    "src/ui/browser.h"
    "src/ui/command.cpp"
    "src/ui/command.h"
    "src/ui/command_manager.cpp"
    "src/ui/command_manager.h"
    "src/ui/controls.cpp"
    "src/ui/controls.h"
    "src/ui/env_editor.cpp"
    "src/ui/env_editor.h"
    "src/ui/file_dialog.cpp"
    "src/ui/file_dialog.h"
    "src/ui/file_dropper.cpp"
    "src/ui/file_dropper.h"
    "src/ui/font.cpp"
    "src/ui/font.h"
    "src/ui/forms.h"
    "src/ui/history.cpp"
    "src/ui/history.h"
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
    "src/settings_data.cpp"
    "src/settings_data.h"
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
    vorbisfile
    midi-parser
    nfd
    vst3-sdk-hosting)

if (WIN32)
    set(WB_LINK_LIBRARIES ${WB_LINK_LIBRARIES} imgui-d3d11 Avrt Dwmapi)
elseif (UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBPULSE REQUIRED IMPORTED_TARGET libpulse)
    set(WB_LINK_LIBRARIES ${WB_LINK_LIBRARIES} PkgConfig::LIBPULSE xcb X11 X11-xcb)
endif ()

add_library(whitebox-lib STATIC ${WB_SOURCES})
target_link_libraries(whitebox-lib PUBLIC ${WB_LINK_LIBRARIES})
target_include_directories(whitebox-lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(whitebox-lib PROPERTIES
    CXX_STANDARD 20
    DEBUG_POSTFIX "-debug")

if (WB_IPO_SUPPORTED)
    set_target_properties(whitebox-lib PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

if (WIN32)
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


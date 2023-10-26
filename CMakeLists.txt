cmake_minimum_required(VERSION 3.16)

if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

project("that-daw")

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /permissive-")
endif ()

include(Dependencies.cmake)

add_subdirectory(assets)

set(DAW_SOURCES
    "src/core/audio_format.h"
    "src/core/audio_buffer.h"
    "src/core/color.h"
    "src/core/debug.cpp"
    "src/core/debug.h"
    "src/core/file.h"
    "src/core/midi.h"
    "src/core/queue.h"

    "src/engine/audio_driver_wasapi.cpp"
    "src/engine/audio_driver_wasapi.h"
    "src/engine/audio_stream.cpp"
    "src/engine/audio_stream.h"
    "src/engine/clip.h"
    "src/engine/engine.cpp"
    "src/engine/engine.h"
    "src/engine/sample.cpp"
    "src/engine/sample.h"
    "src/engine/sample_table.cpp"
    "src/engine/sample_table.h"
    "src/engine/track.cpp"
    "src/engine/track.h"
    "src/engine/track_message.h"
    
    "src/app.cpp"
    "src/app.h"
    "src/app_sdl2.cpp"
    "src/app_sdl2.h"
    "src/app_win32.cpp"
    "src/app_win32.h"
    "src/controls.cpp"
    "src/controls.h"
    "src/def.h"
    "src/global_state.cpp"
    "src/global_state.h"
    "src/gui_content_browser.cpp"
    "src/gui_content_browser.h"
    "src/gui_mixer.cpp"
    "src/gui_mixer.h"
    "src/gui_timeline.cpp"
    "src/gui_timeline.h"
    "src/IconsMaterialDesign.h"
    "src/popup_state_manager.h"
    "src/renderer.cpp"
    "src/renderer.h"
    "src/renderer_d3d11.cpp"
    "src/renderer_d3d11.h"
    "src/stdpch.h"
    "src/types.h"
    "src/waveform_view_buffer.h"
    "src/widget.cpp"
    "src/widget.h"
    "src/widget_test.h")

add_library(daw_lib ${DAW_SOURCES})
set_property(TARGET daw_lib PROPERTY CXX_STANDARD 20)
set_target_properties(daw_lib PROPERTIES CXX_EXTENSIONS OFF)

target_precompile_headers(daw_lib
    PRIVATE
        "src/core/debug.h"
        "src/types.h"
        "src/stdpch.h")

target_link_libraries(daw_lib PUBLIC
    imgui-sdl2
    imgui-d3d11
    imgui-freetype
    imgui-cpp
    spdlog::spdlog
    sndfile
    SDL2::SDL2main)

if ((CMAKE_C_COMPILER_ID STREQUAL "MSVC") AND (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"))
    set_target_properties(daw_lib PROPERTIES MSVC_DEBUG_INFORMATION_FORMAT $<$<CONFIG:Debug>:EditAndContinue>)
endif ()

add_executable(daw "src/main.cpp")
target_link_libraries(daw PUBLIC daw_lib)
set_property(TARGET daw PROPERTY CXX_STANDARD 20)
set_target_properties(daw PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(daw_lib assets)

add_subdirectory(test)
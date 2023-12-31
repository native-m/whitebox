include(cmake/CPM.cmake)

CPMAddPackage(
    NAME                SDL
    GITHUB_REPOSITORY   libsdl-org/SDL
    VERSION             2.28.3
    GIT_TAG             release-2.28.3
    OPTIONS             "SDL_SHARED OFF"
                        "SDL_TESTS OFF"
                        "SDL_TEST_LIBRARY OFF"
                        "SDL_DISABLE_INSTALL ON"
)

CPMAddPackage(
    NAME                imgui
    VERSION             1.89.9-docking-wip
    GITHUB_REPOSITORY   ocornut/imgui
    GIT_TAG             83c433682883aaa6f9583a4a84d1ae4bd8d1e977
    DOWNLOAD_ONLY       YES
)

if (imgui_ADDED)
    set(IMGUI_SRC_FILES
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui.h"
        "${imgui_SOURCE_DIR}/imgui_demo.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_internal.h"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/imstb_rectpack.h"
        "${imgui_SOURCE_DIR}/imstb_textedit.h"
        "${imgui_SOURCE_DIR}/imstb_truetype.h")

    add_library(imgui STATIC ${IMGUI_SRC_FILES})
    target_include_directories(imgui PUBLIC "${imgui_SOURCE_DIR}")
    target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
    target_precompile_headers(imgui
        PUBLIC
            "${imgui_SOURCE_DIR}/imgui.h"
            "${imgui_SOURCE_DIR}/imgui_internal.h")

    add_library(imgui-backends INTERFACE IMPORTED)
    target_include_directories(imgui-backends INTERFACE "${imgui_SOURCE_DIR}/backends")

    add_library(imgui-freetype STATIC
        "${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp"
        "${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.h")
    target_include_directories(imgui-freetype PUBLIC $<BUILD_INTERFACE:${imgui_SOURCE_DIR}/misc/freetype>)
    target_link_libraries(imgui-freetype PUBLIC imgui freetype)

    set(IMGUI_CPP_SOURCES
        "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
        "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.h")
    add_library(imgui-cpp STATIC ${IMGUI_CPP_SOURCES})
    target_include_directories(imgui-cpp PUBLIC $<BUILD_INTERFACE:${imgui_SOURCE_DIR}/misc/cpp>)
    target_link_libraries(imgui-cpp PUBLIC imgui)

    set(IMGUI_SDL2_SRC_FILES
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.h")
    add_library(imgui-sdl2 STATIC ${IMGUI_SDL2_SRC_FILES})
    target_link_libraries(imgui-sdl2
        PUBLIC SDL2::SDL2-static imgui imgui-backends)

    set(IMGUI_BACKEND_D3D11_SOURCES
        "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.h")
    add_library(imgui-d3d11 STATIC ${IMGUI_BACKEND_D3D11_SOURCES})
    target_include_directories(imgui-d3d11 PUBLIC $<BUILD_INTERFACE:${imgui_SOURCE_DIR}/backends>)
    target_link_libraries(imgui-d3d11 PUBLIC imgui dxguid)

    # set(IMGUI_GL3_SRC_FILES
    #     "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
    #     "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.h")

    # add_library(imgui-gl3 STATIC ${IMGUI_GL3_SRC_FILES})
    # target_link_libraries(imgui-gl3
    #     PUBLIC imgui imgui-backends)
endif()

CPMAddPackage(
    NAME                spdlog
    VERSION             1.11.0
    GITHUB_REPOSITORY   gabime/spdlog
    OPTIONS             "SPDLOG_NO_EXCEPTIONS OFF"
)

CPMAddPackage(
    NAME                freetype
    GITHUB_REPOSITORY   freetype/freetype
    GIT_TAG             VER-2-13-2
    OPTIONS             "FT_DISABLE_ZLIB TRUE"
                        "FT_DISABLE_BZIP2 TRUE"
                        "FT_DISABLE_PNG TRUE"
                        "FT_DISABLE_HARFBUZZ TRUE"
                        "FT_DISABLE_BROTLI TRUE"
)

CPMAddPackage(
    NAME                simdjson
    GITHUB_REPOSITORY   simdjson/simdjson
    VERSION             3.3.0
)

CPMAddPackage(
    NAME                libsndfile
    GITHUB_REPOSITORY   libsndfile/libsndfile
    GIT_TAG             1.1.0
    OPTIONS             "BUILD_PROGRAMS OFF"
                        "BUILD_EXAMPLES OFF"
                        "BUILD_TESTING OFF"
                        "ENABLE_CPACK OFF"
                        "ENABLE_PACKAGE_CONFIG OFF"
                        "INSTALL_PKGCONFIG_MODULE OFF"
)

CPMAddPackage(
    NAME                nativefiledialog-extended
    GITHUB_REPOSITORY   btzy/nativefiledialog-extended
    VERSION             1.1.0
)

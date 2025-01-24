include(cmake/CPM.cmake)

CPMAddPackage(
    NAME                SDL
    GITHUB_REPOSITORY   libsdl-org/SDL
    VERSION             2.30.10
    GIT_TAG             release-2.30.10
    OPTIONS             "SDL_SHARED OFF"
                        "SDL_TESTS OFF"
                        "SDL_TEST_LIBRARY OFF"
                        "SDL2_DISABLE_INSTALL ON"
                        # Disable unused subsystems
                        "SDL_ATOMIC OFF"
                        "SDL_AUDIO OFF" # We have our own audio I/O abstraction
                        "SDL_RENDER OFF"
                        "SDL_JOYSTICK OFF"
                        "SDL_HAPTIC OFF"
    EXCLUDE_FROM_ALL
)

CPMAddPackage(
    NAME                Vulkan-Headers
    GITHUB_REPOSITORY   KhronosGroup/Vulkan-Headers
    VERSION             vulkan-sdk-1.3.268.0
    GIT_TAG             vulkan-sdk-1.3.268.0
)

CPMAddPackage(
    NAME                VulkanMemoryAllocator
    GITHUB_REPOSITORY   GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
    VERSION             3.0.1
    DOWNLOAD_ONLY       YES
)

CPMAddPackage(
    NAME                volk
    GITHUB_REPOSITORY   zeux/volk
    VERSION             1.3.268.0
    GIT_TAG             vulkan-sdk-1.3.268.0
    OPTIONS             "VOLK_PULL_IN_VULKAN OFF"
                        "VOLK_HEADERS_ONLY ON"
                        "VOLK_INSTALL OFF"
)

CPMAddPackage(
    NAME                vk-bootstrap
    GITHUB_REPOSITORY   charles-lunarg/vk-bootstrap
    VERSION             1.3.282
)

CPMAddPackage(
    NAME                imgui
    VERSION             1.91.6-docking
    GITHUB_REPOSITORY   ocornut/imgui
    DOWNLOAD_ONLY       YES
)

CPMAddPackage(
    NAME                implot
    VERSION             0.16
    GITHUB_REPOSITORY   epezent/implot
    DOWNLOAD_ONLY       YES
)

CPMAddPackage(
    NAME                leveldb
    VERSION             1.23
    GIT_TAG             1.23
    GITHUB_REPOSITORY   google/leveldb
    OPTIONS             "LEVELDB_INSTALL OFF"
                        "LEVELDB_BUILD_TESTS OFF"
                        "LEVELDB_BUILD_BENCHMARKS OFF"
    EXCLUDE_FROM_ALL
)

# Workaround for deprecated C++20 STL classes
CPMAddPackage(
    NAME                fmt
    GIT_TAG             10.2.1
    GITHUB_REPOSITORY   fmtlib/fmt
    OPTIONS             "FMT_INSTALL OFF"
    EXCLUDE_FROM_ALL
)

CPMAddPackage(
    NAME                compile-time-regular-expressions
    VERSION             3.9.0
    GITHUB_REPOSITORY   hanickadot/compile-time-regular-expressions
    OPTIONS             "CTRE_BUILD_TESTS OFF"
                        "CTRE_BUILD_PACKAGE OFF"
                        "CTRE_BUILD_PACKAGE_DEB OFF"
                        "CTRE_BUILD_PACKAGE_RPM OFF"
    EXCLUDE_FROM_ALL
)

CPMAddPackage(
    NAME                spdlog
    VERSION             1.13.0
    GITHUB_REPOSITORY   gabime/spdlog
    OPTIONS             "SPDLOG_NO_EXCEPTIONS OFF"
                        "SPDLOG_FMT_EXTERNAL ON"
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
                        "SKIP_INSTALL_HEADERS TRUE"
                        "SKIP_INSTALL_LIBRARIES TRUE"
                        "SKIP_INSTALL_ALL TRUE"
    EXCLUDE_FROM_ALL
)

CPMAddPackage(
    NAME                libsndfile
    GITHUB_REPOSITORY   libsndfile/libsndfile
    GIT_TAG             1.2.2
    OPTIONS             "BUILD_PROGRAMS OFF"
                        "BUILD_EXAMPLES OFF"
                        "BUILD_TESTING OFF"
                        "ENABLE_CPACK OFF"
                        "ENABLE_BOW_DOCS OFF"
                        "ENABLE_EXTERNAL_LIBS OFF"
                        "ENABLE_PACKAGE_CONFIG OFF"
                        "INSTALL_PKGCONFIG_MODULE OFF"
                        "INSTALL_MANPAGES OFF"
    EXCLUDE_FROM_ALL
)

# Required for vorbis
CPMAddPackage(
    NAME                ogg
    GITHUB_REPOSITORY   xiph/ogg
    VERSION             1.3.5
    OPTIONS             "BUILD_TESTING OFF"
                        "INSTALL_DOCS OFF"
                        "INSTALL_PKG_CONFIG_MODULE OFF"
                        "INSTALL_CMAKE_PACKAGE_MODULE OFF"
    EXCLUDE_FROM_ALL
)

CPMAddPackage(
    NAME                vorbis
    GITHUB_REPOSITORY   xiph/vorbis
    VERSION             1.3.8-alpha
    # prior to v1.37, does not support checking existing Ogg target
    GIT_TAG             84c023699cdf023a32fa4ded32019f194afcdad0
    OPTIONS             "INSTALL_CMAKE_PACKAGE_MODULE OFF"
                        "BUILD_TESTING OFF"
    EXCLUDE_FROM_ALL
)

#CPMAddPackage(
#    NAME                faad2
#    GITHUB_REPOSITORY   knik0/faad2
#    VERSION             2.11.1
#    GIT_TAG             2.11.1
#    OPTIONS             "FAAD_BUILD_CLI OFF"
#    EXCLUDE_FROM_ALL
#)

CPMAddPackage(
    NAME                midi_parser
    GITHUB_REPOSITORY   abique/midi-parser
    GIT_TAG             ddc815b44c0cb05fa133f9630355f715daabb380
    VERSION             1.0.0
    DOWNLOAD_ONLY       YES
)

CPMAddPackage(
    NAME                nativefiledialog-extended
    GITHUB_REPOSITORY   btzy/nativefiledialog-extended
    VERSION             1.1.0
)

CPMAddPackage(
    NAME                vst3sdk
    VERSION             3.7.11
    URL                 https://download.steinberg.net/sdk_downloads/vst-sdk_3.7.11_build-10_2024-04-22.zip
    DOWNLOAD_ONLY       YES
)

if (UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBPULSE REQUIRED IMPORTED_TARGET libpulse)
endif()

# Here we define targets that are not using CMake
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
    target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS ImDrawIdx=ImU32)
    target_precompile_headers(imgui
        PUBLIC
            "$<$<COMPILE_LANGUAGE:CXX>:${imgui_SOURCE_DIR}/imgui.h>"
            "$<$<COMPILE_LANGUAGE:CXX>:${imgui_SOURCE_DIR}/imgui_internal.h>")
    if (WB_IPO_SUPPORTED)
        set_target_properties(imgui PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()

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

    if(WIN32)
        set(IMGUI_BACKEND_D3D11_SOURCES
            "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
            "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.h")
        add_library(imgui-d3d11 STATIC ${IMGUI_BACKEND_D3D11_SOURCES})
        target_include_directories(imgui-d3d11 PUBLIC $<BUILD_INTERFACE:${imgui_SOURCE_DIR}/backends>)
        target_link_libraries(imgui-d3d11 PUBLIC imgui dxguid)
    endif()

    set(IMGUI_BACKEND_VULKAN_SOURCES
        "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.h")
    add_library(imgui-vulkan STATIC ${IMGUI_BACKEND_VULKAN_SOURCES})
    target_include_directories(imgui-vulkan PUBLIC $<BUILD_INTERFACE:${imgui_SOURCE_DIR}/backends>)
    target_link_libraries(imgui-vulkan PUBLIC imgui Vulkan-Headers)
    target_compile_definitions(imgui-vulkan PRIVATE VK_NO_PROTOTYPES)

    # set(IMGUI_GL3_SRC_FILES
    #     "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
    #     "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.h")

    # add_library(imgui-gl3 STATIC ${IMGUI_GL3_SRC_FILES})
    # target_link_libraries(imgui-gl3
    #     PUBLIC imgui imgui-backends)
endif()

if (implot_ADDED)
    set(IMPLOT_SRC_FILES
        "${implot_SOURCE_DIR}/implot.cpp"
        "${implot_SOURCE_DIR}/implot.h"
        "${implot_SOURCE_DIR}/implot_demo.cpp"
        "${implot_SOURCE_DIR}/implot_internal.h"
        "${implot_SOURCE_DIR}/implot_items.cpp")
    add_library(implot STATIC ${IMPLOT_SRC_FILES})
    target_include_directories(implot PUBLIC "${implot_SOURCE_DIR}")
    target_link_libraries(implot PUBLIC imgui)
    if (WB_IPO_SUPPORTED)
        set_target_properties(implot PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endif()

if (VulkanMemoryAllocator_ADDED)
    add_library(VulkanMemoryAllocator INTERFACE)
    target_include_directories(VulkanMemoryAllocator INTERFACE "${VulkanMemoryAllocator_SOURCE_DIR}/include")
endif()

if (midi_parser_ADDED)
    add_library(midi-parser "${midi_parser_SOURCE_DIR}/src/midi-parser.c")
    target_include_directories(midi-parser PUBLIC "${midi_parser_SOURCE_DIR}/include")
endif()

if (vst3sdk_ADDED)
    # VST3 Stuff
    set(VST3_SDK_PATH "${vst3sdk_SOURCE_DIR}/vst3sdk")
    set(VST3_BASE_PATH "${VST3_SDK_PATH}/base")
    set(VST3_BASE_SRC
        "${VST3_BASE_PATH}/source/baseiids.cpp"
        "${VST3_BASE_PATH}/source/classfactoryhelpers.h"
        "${VST3_BASE_PATH}/source/fbuffer.cpp"
        "${VST3_BASE_PATH}/source/fbuffer.h"
        "${VST3_BASE_PATH}/source/fcleanup.h"
        "${VST3_BASE_PATH}/source/fcommandline.h"
        "${VST3_BASE_PATH}/source/fdebug.cpp"
        "${VST3_BASE_PATH}/source/fdebug.h"
        "${VST3_BASE_PATH}/source/fdynlib.cpp"
        "${VST3_BASE_PATH}/source/fdynlib.h"
        "${VST3_BASE_PATH}/source/fobject.cpp"
        "${VST3_BASE_PATH}/source/fobject.h"
        "${VST3_BASE_PATH}/source/fstreamer.cpp"
        "${VST3_BASE_PATH}/source/fstreamer.h"
        "${VST3_BASE_PATH}/source/fstring.cpp"
        "${VST3_BASE_PATH}/source/fstring.h"
        "${VST3_BASE_PATH}/source/timer.cpp"
        "${VST3_BASE_PATH}/source/timer.h"
        "${VST3_BASE_PATH}/source/updatehandler.cpp"
        "${VST3_BASE_PATH}/source/updatehandler.h"
        "${VST3_BASE_PATH}/thread/include/fcondition.h"
        "${VST3_BASE_PATH}/thread/include/flock.h"
        "${VST3_BASE_PATH}/thread/source/fcondition.cpp"
        "${VST3_BASE_PATH}/thread/source/flock.cpp")
    add_library(vst3-base STATIC ${VST3_BASE_SRC})
    target_include_directories(vst3-base PUBLIC "${VST3_SDK_PATH}")
    target_compile_options(vst3-base
        PUBLIC
            "$<$<CONFIG:Debug>:-DDEVELOPMENT=1>"
            "$<$<CONFIG:Release>:-DRELEASE=1>"
            "$<$<CONFIG:RelWithDebInfo>:-DRELEASE=1>"
    )
    
    set(VST3_PLUGINTERFACES_PATH "${VST3_SDK_PATH}/pluginterfaces")
    set(VST3_PLUGINTERFACES_SRC
        "${VST3_PLUGINTERFACES_PATH}/base/conststringtable.cpp"
        "${VST3_PLUGINTERFACES_PATH}/base/conststringtable.h"
        "${VST3_PLUGINTERFACES_PATH}/base/coreiids.cpp"
        "${VST3_PLUGINTERFACES_PATH}/base/falignpop.h"
        "${VST3_PLUGINTERFACES_PATH}/base/falignpush.h"
        "${VST3_PLUGINTERFACES_PATH}/base/fplatform.h"
        "${VST3_PLUGINTERFACES_PATH}/base/fstrdefs.h"
        "${VST3_PLUGINTERFACES_PATH}/base/ftypes.h"
        "${VST3_PLUGINTERFACES_PATH}/base/funknown.cpp"
        "${VST3_PLUGINTERFACES_PATH}/base/funknown.h"
        "${VST3_PLUGINTERFACES_PATH}/base/funknownimpl.h"
        "${VST3_PLUGINTERFACES_PATH}/base/futils.h"
        "${VST3_PLUGINTERFACES_PATH}/base/fvariant.h"
        "${VST3_PLUGINTERFACES_PATH}/base/geoconstants.h"
        "${VST3_PLUGINTERFACES_PATH}/base/ibstream.h"
        "${VST3_PLUGINTERFACES_PATH}/base/icloneable.h"
        "${VST3_PLUGINTERFACES_PATH}/base/ierrorcontext.h"
        "${VST3_PLUGINTERFACES_PATH}/base/ipersistent.h"
        "${VST3_PLUGINTERFACES_PATH}/base/ipluginbase.h"
        "${VST3_PLUGINTERFACES_PATH}/base/istringresult.h"
        "${VST3_PLUGINTERFACES_PATH}/base/iupdatehandler.h"
        "${VST3_PLUGINTERFACES_PATH}/base/keycodes.h"
        "${VST3_PLUGINTERFACES_PATH}/base/pluginbasefwd.h"
        "${VST3_PLUGINTERFACES_PATH}/base/smartpointer.h"
        "${VST3_PLUGINTERFACES_PATH}/base/typesizecheck.h"
        "${VST3_PLUGINTERFACES_PATH}/base/ucolorspec.h"
        "${VST3_PLUGINTERFACES_PATH}/base/ustring.cpp"
        "${VST3_PLUGINTERFACES_PATH}/base/ustring.h"
        "${VST3_PLUGINTERFACES_PATH}/gui/iplugview.h"
        "${VST3_PLUGINTERFACES_PATH}/gui/iplugviewcontentscalesupport.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstattributes.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstaudioprocessor.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstautomationstate.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstchannelcontextinfo.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstcomponent.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstcontextmenu.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivsteditcontroller.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstevents.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivsthostapplication.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstinterappaudio.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstmessage.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstmidicontrollers.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstmidilearn.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstnoteexpression.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstparameterchanges.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstparameterfunctionname.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstphysicalui.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstpluginterfacesupport.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstplugview.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstprefetchablesupport.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstprocesscontext.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstrepresentation.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/ivstunits.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/vstpresetkeys.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/vstpshpack4.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/vstspeaker.h"
        "${VST3_PLUGINTERFACES_PATH}/vst/vsttypes.h")
    add_library(vst3-pluginterfaces STATIC ${VST3_PLUGINTERFACES_SRC})
    target_link_libraries(vst3-pluginterfaces PUBLIC vst3-base)
    
    set(VST3_PUBLICSDK_PATH "${VST3_SDK_PATH}/public.sdk")
    set(VST3_SDK_COMMON_SRC
        "${VST3_PUBLICSDK_PATH}/source/common/commoniids.cpp"
        "${VST3_PUBLICSDK_PATH}/source/common/openurl.cpp"
        "${VST3_PUBLICSDK_PATH}/source/common/openurl.h"
        "${VST3_PUBLICSDK_PATH}/source/common/readfile.cpp"
        "${VST3_PUBLICSDK_PATH}/source/common/readfile.h"
        "${VST3_PUBLICSDK_PATH}/source/common/systemclipboard.h"
        "${VST3_PUBLICSDK_PATH}/source/common/threadchecker.h"
        "${VST3_PUBLICSDK_PATH}/source/common/threadchecker_linux.cpp")
    
    if (WIN32)
        set(VST3_SDK_COMMON_SRC ${VST3_SDK_COMMON_SRC}
            "${VST3_PUBLICSDK_PATH}/source/common/systemclipboard_win32.cpp"
            "${VST3_PUBLICSDK_PATH}/source/common/threadchecker_win32.cpp"
        )
    elseif (LINUX)
        set(VST3_SDK_COMMON_SRC ${VST3_SDK_COMMON_SRC}
            "${VST3_PUBLICSDK_PATH}/source/common/systemclipboard_linux.cpp"
            "${VST3_PUBLICSDK_PATH}/source/common/threadchecker_linux.cpp")
    endif ()
    
    add_library(vst3-sdk-common STATIC ${VST3_SDK_COMMON_SRC})
    target_link_libraries(vst3-sdk-common PUBLIC vst3-pluginterfaces)
    
    set(VST3_SDK_HOSTING_SRC
        "${VST3_PUBLICSDK_PATH}/source/vst/hosting/connectionproxy.cpp"
        "${VST3_PUBLICSDK_PATH}/source/vst/hosting/connectionproxy.h"
        "${VST3_PUBLICSDK_PATH}/source/vst/hosting/eventlist.cpp"
        "${VST3_PUBLICSDK_PATH}/source/vst/hosting/eventlist.h"
        "${VST3_PUBLICSDK_PATH}/source/vst/hosting/hostclasses.cpp"
        "${VST3_PUBLICSDK_PATH}/source/vst/hosting/hostclasses.h"
        "${VST3_PUBLICSDK_PATH}/source/vst/hosting/module.cpp"
        "${VST3_PUBLICSDK_PATH}/source/vst/hosting/module.h"
        
        "${VST3_PUBLICSDK_PATH}/source/vst/hosting/parameterchanges.cpp"
        "${VST3_PUBLICSDK_PATH}/source/vst/hosting/parameterchanges.h"
        "${VST3_PUBLICSDK_PATH}/source/vst/hosting/pluginterfacesupport.cpp"
        "${VST3_PUBLICSDK_PATH}/source/vst/hosting/pluginterfacesupport.h"
        "${VST3_PUBLICSDK_PATH}/source/vst/hosting/plugprovider.cpp"
        "${VST3_PUBLICSDK_PATH}/source/vst/hosting/plugprovider.h"
        "${VST3_PUBLICSDK_PATH}/source/vst/hosting/processdata.cpp"
        "${VST3_PUBLICSDK_PATH}/source/vst/hosting/processdata.h"
        "${VST3_PUBLICSDK_PATH}/source/vst/utility/optional.h"
        "${VST3_PUBLICSDK_PATH}/source/vst/utility/stringconvert.cpp"
        "${VST3_PUBLICSDK_PATH}/source/vst/utility/stringconvert.h"
        "${VST3_PUBLICSDK_PATH}/source/vst/utility/uid.h"
        "${VST3_PUBLICSDK_PATH}/source/vst/utility/versionparser.h"
        "${VST3_PUBLICSDK_PATH}/source/vst/vstinitiids.cpp")
    
    if (WIN32)
        set(VST3_SDK_HOSTING_SRC ${VST3_SDK_HOSTING_SRC}
            "${VST3_PUBLICSDK_PATH}/source/vst/hosting/module_win32.cpp")
    elseif (LINUX)
        set(VST3_SDK_HOSTING_SRC ${VST3_SDK_HOSTING_SRC}
            "${VST3_PUBLICSDK_PATH}/source/vst/hosting/module_linux.cpp")
    endif ()
    
    add_library(vst3-sdk-hosting STATIC ${VST3_SDK_HOSTING_SRC})
    target_link_libraries(vst3-sdk-hosting PUBLIC vst3-sdk-common)
endif()
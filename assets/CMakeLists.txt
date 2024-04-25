find_program(FXC fxc)
find_program(GLSLC glslc)

macro(wb_add_shader_d3d11 _TARGET _FILE)
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${_FILE}.dxbc"
        COMMAND FXC
        ARGS /T ${_TARGET} /Fo "${CMAKE_CURRENT_BINARY_DIR}/${_FILE}.dxbc" "${CMAKE_CURRENT_SOURCE_DIR}/${_FILE}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_FILE}"
        VERBATIM)
    set(WB_ASSET_DEPENDENCIES "${WB_ASSET_DEPENDENCIES};${CMAKE_CURRENT_BINARY_DIR}/${_FILE}.dxbc")
endmacro()

macro(wb_add_shader_glsl _TARGET _FILE)
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${_FILE}.spv"
        COMMAND glslc
        ARGS
            --target-env=vulkan1.0
            -fshader-stage=${_TARGET}
            -o "${CMAKE_CURRENT_BINARY_DIR}/${_FILE}.spv"
            "${CMAKE_CURRENT_SOURCE_DIR}/${_FILE}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_FILE}"
        VERBATIM)
    set(WB_ASSET_DEPENDENCIES "${WB_ASSET_DEPENDENCIES};${CMAKE_CURRENT_BINARY_DIR}/${_FILE}.spv")
endmacro()

macro(wb_add_asset_file _FILE)
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${_FILE}"
        COMMAND cmake
        ARGS -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${_FILE}" "${CMAKE_CURRENT_BINARY_DIR}/${_FILE}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_FILE}"
        VERBATIM)
    set(WB_ASSET_DEPENDENCIES "${WB_ASSET_DEPENDENCIES};${CMAKE_CURRENT_BINARY_DIR}/${_FILE}")
endmacro()

if (MSVC)
    wb_add_shader_d3d11(vs_5_0 waveform2_aa_vs.hlsl)
    wb_add_shader_d3d11(vs_5_0 waveform2_vs.hlsl)
    wb_add_shader_d3d11(ps_5_0 waveform_aa_ps.hlsl)
endif()

wb_add_shader_glsl(fragment waveform_aa.fs)
wb_add_asset_file("Inter-Medium.otf")

add_custom_target(wb-assets DEPENDS ${WB_ASSET_DEPENDENCIES})

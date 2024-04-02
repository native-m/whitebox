find_program(FXC fxc)

macro(wb_add_shader_d3d11 _TARGET _FILE)
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${_FILE}.dxbc"
        COMMAND FXC
        ARGS /T ${_TARGET} /Fo "${CMAKE_CURRENT_BINARY_DIR}/${_FILE}.dxbc" "${CMAKE_CURRENT_SOURCE_DIR}/${_FILE}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_FILE}"
        VERBATIM)
    set(WB_SHADER_DEPENDENCIES "${WB_SHADER_DEPENDENCIES};${CMAKE_CURRENT_BINARY_DIR}/${_FILE}.dxbc")
endmacro()

wb_add_shader_d3d11(vs_5_0 waveform2_aa_vs.hlsl)
wb_add_shader_d3d11(vs_5_0 waveform2_vs.hlsl)
wb_add_shader_d3d11(ps_5_0 waveform_aa_ps.hlsl)

add_custom_target(wb-assets DEPENDS ${WB_SHADER_DEPENDENCIES})

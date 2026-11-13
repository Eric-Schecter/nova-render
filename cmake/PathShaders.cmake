option(NOVA_ENABLE_PATH_TRACING "Build the native progressive path tracer" ON)
if(NOT NOVA_ENABLE_PATH_TRACING)
    return()
endif()
set(_path_source "${PROJECT_SOURCE_DIR}/render/shaders/pathtrace.hlsl")
if(NOVA_ENABLE_VULKAN OR APPLE)
    set(_path_spv "${SHADER_OUTPUT_DIR}/pathtrace.compute.spv")
    add_custom_command(OUTPUT "${_path_spv}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
        COMMAND "${NOVA_DXC_EXECUTABLE}" -spirv -fspv-target-env=vulkan1.2 -T cs_6_5 -E CSMain "${_path_source}" -Fo "${_path_spv}"
        DEPENDS "${_path_source}" "${NOVA_DXC_EXECUTABLE}" VERBATIM)
    if(NOVA_ENABLE_VULKAN)
        list(APPEND SHADER_OUTPUTS "${_path_spv}")
    endif()
endif()
if(APPLE)
    set(_path_metal "${SHADER_OUTPUT_DIR}/pathtrace.metallib")
    add_custom_command(OUTPUT "${_path_metal}"
        COMMAND "${NOVA_SPIRV_CROSS_EXECUTABLE}" "${_path_spv}" --msl --msl-version 30000 --msl-decoration-binding --output "${SHADER_OUTPUT_DIR}/pathtrace.metal"
        COMMAND "${NOVA_XCRUN_EXECUTABLE}" -sdk macosx metal -std=metal3.0 -c "${SHADER_OUTPUT_DIR}/pathtrace.metal" -o "${SHADER_OUTPUT_DIR}/pathtrace.air"
        COMMAND "${NOVA_XCRUN_EXECUTABLE}" -sdk macosx metallib "${SHADER_OUTPUT_DIR}/pathtrace.air" -o "${_path_metal}"
        DEPENDS "${_path_spv}" "${NOVA_SPIRV_CROSS_EXECUTABLE}" VERBATIM)
    list(APPEND SHADER_OUTPUTS "${_path_metal}")
elseif(WIN32)
    set(_path_dxil "${SHADER_OUTPUT_DIR}/pathtrace.compute.dxil")
    add_custom_command(OUTPUT "${_path_dxil}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
        COMMAND "${NOVA_DXC_EXECUTABLE}" -T cs_6_5 -E CSMain "${_path_source}" -Fo "${_path_dxil}"
        DEPENDS "${_path_source}" "${NOVA_DXC_EXECUTABLE}" VERBATIM)
    list(APPEND SHADER_OUTPUTS "${_path_dxil}")
endif()
add_custom_target(nova_path_shaders DEPENDS ${_path_spv} ${_path_metal} ${_path_dxil})

# Shader compilers are host tools. SDK paths also cover hosts without a vcpkg DXC port.
file(GLOB _nova_sdk_bins "$ENV{HOME}/VulkanSDK/*/macOS/bin")
list(SORT _nova_sdk_bins COMPARE NATURAL ORDER DESCENDING)
find_program(NOVA_DXC_EXECUTABLE NAMES dxc
    HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/directx-dxc"
          "$ENV{VULKAN_SDK}/bin" ${_nova_sdk_bins} REQUIRED)
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

if(APPLE)
    find_program(NOVA_SPIRV_CROSS_EXECUTABLE NAMES spirv-cross
        HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/spirv-cross"
              "$ENV{VULKAN_SDK}/bin" ${_nova_sdk_bins} REQUIRED)
    find_program(NOVA_XCRUN_EXECUTABLE xcrun REQUIRED)
endif()
foreach(shader_name triangle model skybox model-motion sky-motion model-gbuffer model-deferred model-oit)
if(shader_name STREQUAL "skybox" OR shader_name MATCHES "motion$" OR shader_name MATCHES "^model-(gbuffer|deferred|oit)$")
    set(_hlsl "${PROJECT_SOURCE_DIR}/render/shaders/model.hlsl")
else()
    set(_hlsl "${PROJECT_SOURCE_DIR}/render/shaders/${shader_name}.hlsl")
endif()
set(METAL_AIR)
foreach(stage vert frag)
    if(stage STREQUAL "vert")
        set(entry VSMain)
        set(profile vs_6_0)
        set(msl_entry vertexMain)
        set(vulkan_options -fvk-invert-y)
    else()
        set(entry PSMain)
        set(profile ps_6_0)
        set(msl_entry fragmentMain)
        set(vulkan_options)
    endif()
    if(shader_name STREQUAL "skybox")
        if(stage STREQUAL "vert")
            set(entry SkyVSMain)
        else()
            set(entry SkyPSMain)
        endif()
    endif()
    if(shader_name STREQUAL "model-motion")
        if(stage STREQUAL "vert")
            set(entry MotionVSMain)
        else()
            set(entry MotionPSMain)
        endif()
    elseif(shader_name STREQUAL "sky-motion")
        if(stage STREQUAL "vert")
            set(entry SkyVSMain)
        else()
            set(entry SkyMotionPSMain)
        endif()
    endif()
    if(shader_name STREQUAL "model-gbuffer" AND stage STREQUAL "frag")
        set(entry GBufferPSMain)
    elseif(shader_name STREQUAL "model-oit" AND stage STREQUAL "frag")
        set(entry OITPSMain)
    elseif(shader_name STREQUAL "model-deferred")
        if(stage STREQUAL "vert")
            set(entry SkyVSMain)
        else()
            set(entry DeferredPSMain)
        endif()
    endif()
    if(NOVA_ENABLE_VULKAN)
        set(output "${SHADER_OUTPUT_DIR}/${shader_name}.${stage}.spv")
        add_custom_command(OUTPUT "${output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
            COMMAND "${NOVA_DXC_EXECUTABLE}" -spirv -fspv-target-env=vulkan1.0
                ${vulkan_options} -T ${profile} -E ${entry} -Fo "${output}" "${_hlsl}"
            DEPENDS "${_hlsl}" "${NOVA_DXC_EXECUTABLE}" VERBATIM)
        list(APPEND SHADER_OUTPUTS "${output}")
    endif()
    if(WIN32)
        set(output "${SHADER_OUTPUT_DIR}/${shader_name}.${stage}.dxil")
        add_custom_command(OUTPUT "${output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
            COMMAND "${NOVA_DXC_EXECUTABLE}" -DNOVA_D3D12=1 -T ${profile} -E ${entry} -Fo "${output}" "${_hlsl}"
            DEPENDS "${_hlsl}" "${NOVA_DXC_EXECUTABLE}" VERBATIM)
        list(APPEND SHADER_OUTPUTS "${output}")
    elseif(APPLE)
        # Metal uses D3D's Y convention: never reuse the Y-inverted Vulkan module.
        set(spv "${SHADER_OUTPUT_DIR}/${shader_name}.${stage}.metal.spv")
        set(msl "${SHADER_OUTPUT_DIR}/${shader_name}.${stage}.metal")
        set(air "${SHADER_OUTPUT_DIR}/${shader_name}.${stage}.air")
        add_custom_command(OUTPUT "${spv}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
            COMMAND "${NOVA_DXC_EXECUTABLE}" -spirv -fspv-target-env=vulkan1.0
                -T ${profile} -E ${entry} -Fo "${spv}" "${_hlsl}"
            DEPENDS "${_hlsl}" "${NOVA_DXC_EXECUTABLE}" VERBATIM)
        add_custom_command(OUTPUT "${msl}"
            COMMAND "${NOVA_SPIRV_CROSS_EXECUTABLE}" "${spv}" --msl --msl-version 20100 --msl-decoration-binding
                --rename-entry-point ${entry} ${msl_entry} ${stage} --output "${msl}"
            DEPENDS "${spv}" "${NOVA_SPIRV_CROSS_EXECUTABLE}" VERBATIM)
        add_custom_command(OUTPUT "${air}"
            COMMAND "${NOVA_XCRUN_EXECUTABLE}" -sdk macosx metal -std=macos-metal2.1 -c "${msl}" -o "${air}"
            DEPENDS "${msl}" VERBATIM)
        list(APPEND METAL_AIR "${air}")
    endif()
endforeach()
if(APPLE)
    set(output "${SHADER_OUTPUT_DIR}/${shader_name}.metallib")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${NOVA_XCRUN_EXECUTABLE}" -sdk macosx metallib ${METAL_AIR} -o "${output}"
        DEPENDS ${METAL_AIR} VERBATIM)
    list(APPEND SHADER_OUTPUTS "${output}")
endif()
endforeach()
add_custom_target(triangle_shaders DEPENDS ${SHADER_OUTPUTS})
message(STATUS "Shader DXC: ${NOVA_DXC_EXECUTABLE}")

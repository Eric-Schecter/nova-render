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
    set(NOVA_METAL_STD macos-metal2.1)
    set(NOVA_METAL_TARGET_ARGS)
    set(NOVA_METAL_PLATFORM_ARGS)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(_nova_ios_sdk iphoneos)
        set(_nova_ios_suffix)
        if(CMAKE_OSX_SYSROOT MATCHES "iphonesimulator|iPhoneSimulator")
            set(_nova_ios_sdk iphonesimulator)
            set(_nova_ios_suffix -simulator)
        endif()
        set(_nova_ios_version "${CMAKE_OSX_DEPLOYMENT_TARGET}")
        if(NOT _nova_ios_version)
            set(_nova_ios_version 16.0)
        endif()
        execute_process(COMMAND "${NOVA_XCRUN_EXECUTABLE}" -sdk "${_nova_ios_sdk}" --show-sdk-path
            COMMAND_ERROR_IS_FATAL ANY OUTPUT_VARIABLE _nova_ios_sysroot OUTPUT_STRIP_TRAILING_WHITESPACE)
        # Invoke the host Metal toolchain with the selected iOS AIR target and SDK.
        set(NOVA_METAL_TARGET_ARGS -target "air64-apple-ios${_nova_ios_version}${_nova_ios_suffix}" -isysroot "${_nova_ios_sysroot}")
        set(NOVA_METAL_STD ios-metal2.1)
        set(NOVA_METAL_PLATFORM_ARGS --msl-ios)
    endif()
endif()
foreach(shader_name triangle model skybox model-motion sky-motion model-gbuffer model-deferred model-oit model-hybrid-shadow model-hybrid-shadow-compact model-shadow-classify model-shadow-trace model-hybrid-ao model-hybrid-reflection model-probe-deferred model-deferred-half model-probe-deferred-half model-probe-reflection model-probe-update model-probe-update-shared)
if(shader_name STREQUAL "skybox" OR shader_name MATCHES "motion$" OR shader_name MATCHES "^model-(gbuffer|deferred|deferred-half|oit|hybrid-shadow|hybrid-reflection|probe-deferred|probe-deferred-half|probe-reflection|probe-update|probe-update-shared)$")
    set(_hlsl "${PROJECT_SOURCE_DIR}/render/shaders/model.hlsl")
else()
    set(_hlsl "${PROJECT_SOURCE_DIR}/render/shaders/${shader_name}.hlsl")
endif()
set(METAL_AIR)
set(_nova_stages vert frag)
set(_nova_shader_defines)
set(_nova_shader_extra_deps)
if(shader_name MATCHES "^model-(hybrid-(ao|shadow(-compact)?|reflection)|probe-reflection|shadow-(classify|trace))$")
    set(_hlsl "${PROJECT_SOURCE_DIR}/render/shaders/model-hybrid-blue-noise.hlsl")
    set(_nova_shader_extra_deps
        "${PROJECT_SOURCE_DIR}/render/shaders/ao-blue-noise.hlsli"
        "${PROJECT_SOURCE_DIR}/render/shaders/model.hlsl")
endif()
list(APPEND _nova_shader_extra_deps "${PROJECT_SOURCE_DIR}/render/shaders/material-sheen.hlsli" "${PROJECT_SOURCE_DIR}/render/shaders/material-iridescence.hlsli" "${PROJECT_SOURCE_DIR}/render/shaders/material-anisotropy.hlsli")
if(shader_name MATCHES "^model-probe-update(-shared)?$")
    set(_nova_stages comp)
    if(shader_name STREQUAL "model-probe-update-shared")
        set(_nova_shader_defines -DNOVA_SHARED_PROBE_RAYS=1)
    endif()
elseif(shader_name MATCHES "^model-probe-(deferred|reflection)")
    set(_nova_shader_defines -DNOVA_HYBRID_PROBES=1)
endif()
if(shader_name MATCHES "-deferred-half$")
    list(APPEND _nova_shader_defines -DNOVA_HALF_REFLECTIONS=1)
endif()
if(shader_name STREQUAL "model-hybrid-shadow-compact")
    list(APPEND _nova_shader_defines -DNOVA_COMPACT_SHADOWS=1)
endif()
if(shader_name MATCHES "^model-shadow-(classify|trace)$")
    set(_nova_stages comp)
    list(APPEND _nova_shader_defines -DNOVA_SPARSE_SHADOWS=1)
    if(shader_name STREQUAL "model-shadow-classify")
        list(APPEND _nova_shader_defines -DNOVA_SHADOW_CLASSIFY=1)
    endif()
endif()
foreach(stage IN LISTS _nova_stages)
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
    if(stage STREQUAL "comp")
        if(shader_name STREQUAL "model-shadow-classify")
            set(entry HybridShadowClassifyCS)
        elseif(shader_name STREQUAL "model-shadow-trace")
            set(entry HybridShadowTraceCS)
        else()
            set(entry HybridProbeUpdateCS)
        endif()
        set(profile cs_6_0)
        set(msl_entry computeMain)
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
    elseif(shader_name MATCHES "^model-(hybrid-(shadow(-compact)?|reflection|ao)|probe-reflection)$")
        if(stage STREQUAL "vert")
            set(entry SkyVSMain)
        else()
            if(shader_name STREQUAL "model-hybrid-ao")
                set(entry HybridAOPSMain)
            elseif(shader_name MATCHES "-reflection$")
                set(entry HybridReflectionPSMain)
            else()
                set(entry HybridShadowPSMain)
            endif()
        endif()
    elseif(shader_name MATCHES "^model-(probe-)?deferred(-half)?$")
        if(stage STREQUAL "vert")
            set(entry SkyVSMain)
        else()
            set(entry DeferredPSMain)
        endif()
    endif()
    if(NOVA_ENABLE_VULKAN)
        if(stage STREQUAL "frag" AND shader_name MATCHES "^(triangle|model|skybox)$")
            list(APPEND vulkan_options -DNOVA_VULKAN_PRESENT=1)
        endif()
        set(output "${SHADER_OUTPUT_DIR}/${shader_name}.${stage}.spv")
        add_custom_command(OUTPUT "${output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
            COMMAND "${NOVA_DXC_EXECUTABLE}" -spirv -fspv-target-env=vulkan1.0
                ${_nova_shader_defines} ${vulkan_options} -T ${profile} -E ${entry} -Fo "${output}" "${_hlsl}"
            DEPENDS "${_hlsl}" ${_nova_shader_extra_deps} "${NOVA_DXC_EXECUTABLE}" VERBATIM)
        list(APPEND SHADER_OUTPUTS "${output}")
    endif()
    if(WIN32)
        set(output "${SHADER_OUTPUT_DIR}/${shader_name}.${stage}.dxil")
        add_custom_command(OUTPUT "${output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
            COMMAND "${NOVA_DXC_EXECUTABLE}" -DNOVA_D3D12=1 ${_nova_shader_defines} -T ${profile} -E ${entry} -Fo "${output}" "${_hlsl}"
            DEPENDS "${_hlsl}" ${_nova_shader_extra_deps} "${NOVA_DXC_EXECUTABLE}" VERBATIM)
        list(APPEND SHADER_OUTPUTS "${output}")
    elseif(APPLE)
        # Metal uses D3D's Y convention: never reuse the Y-inverted Vulkan module.
        set(spv "${SHADER_OUTPUT_DIR}/${shader_name}.${stage}.metal.spv")
        set(msl "${SHADER_OUTPUT_DIR}/${shader_name}.${stage}.metal")
        set(air "${SHADER_OUTPUT_DIR}/${shader_name}.${stage}.air")
        add_custom_command(OUTPUT "${spv}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
            COMMAND "${NOVA_DXC_EXECUTABLE}" -spirv -fspv-target-env=vulkan1.0
                ${_nova_shader_defines} -T ${profile} -E ${entry} -Fo "${spv}" "${_hlsl}"
            DEPENDS "${_hlsl}" ${_nova_shader_extra_deps} "${NOVA_DXC_EXECUTABLE}" VERBATIM)
        add_custom_command(OUTPUT "${msl}"
            COMMAND "${NOVA_SPIRV_CROSS_EXECUTABLE}" "${spv}" --msl ${NOVA_METAL_PLATFORM_ARGS} --msl-version 20100 --msl-decoration-binding
                --rename-entry-point ${entry} ${msl_entry} ${stage} --output "${msl}"
            DEPENDS "${spv}" "${NOVA_SPIRV_CROSS_EXECUTABLE}" VERBATIM)
        add_custom_command(OUTPUT "${air}"
            COMMAND "${NOVA_XCRUN_EXECUTABLE}" -sdk macosx metal ${NOVA_METAL_TARGET_ARGS} "-std=${NOVA_METAL_STD}" -c "${msl}" -o "${air}"
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
if(NOVA_ENABLE_VULKAN)
    foreach(kind forward present gbuffer)
        set(bindless_opts)
        set(bindless_entry PSMain)
        if(kind STREQUAL "present")
            set(bindless_opts -DNOVA_VULKAN_PRESENT=1)
        elseif(kind STREQUAL "gbuffer")
            set(bindless_entry GBufferPSMain)
        endif()
        set(output "${SHADER_OUTPUT_DIR}/model-bindless-${kind}.frag.spv")
        add_custom_command(OUTPUT "${output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
            COMMAND "${NOVA_DXC_EXECUTABLE}" -spirv -fspv-target-env=vulkan1.2
                -DNOVA_INDEXED_MATERIALS=1 ${bindless_opts} -T ps_6_0 -E ${bindless_entry} -Fo "${output}"
                "${PROJECT_SOURCE_DIR}/render/shaders/model.hlsl"
            DEPENDS "${PROJECT_SOURCE_DIR}/render/shaders/model.hlsl" ${_nova_shader_extra_deps}
                "${NOVA_DXC_EXECUTABLE}" VERBATIM)
        list(APPEND SHADER_OUTPUTS "${output}")
    endforeach()
endif()
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    foreach(kind model model-gbuffer)
        set(entry PSMain)
        if(kind STREQUAL "model-gbuffer")
            set(entry GBufferPSMain)
        endif()
        set(base "${SHADER_OUTPUT_DIR}/${kind}-indexed.frag")
        set(output "${SHADER_OUTPUT_DIR}/${kind}-indexed.metallib")
        add_custom_command(OUTPUT "${base}.air"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
            COMMAND "${NOVA_DXC_EXECUTABLE}" -spirv -fspv-target-env=vulkan1.2
                -DNOVA_INDEXED_MATERIALS=1 -DNOVA_METAL_INDEXED_MATERIALS=1
                -T ps_6_0 -E ${entry} -Fo "${base}.spv" "${PROJECT_SOURCE_DIR}/render/shaders/model.hlsl"
            COMMAND "${NOVA_SPIRV_CROSS_EXECUTABLE}" "${base}.spv" --msl --msl-version 20100
                --msl-decoration-binding --msl-argument-buffers --msl-argument-buffer-tier 1
                --msl-discrete-descriptor-set 0 --msl-discrete-descriptor-set 1 --msl-discrete-descriptor-set 2
                --rename-entry-point ${entry} fragmentMain frag --output "${base}.metal"
            COMMAND "${NOVA_XCRUN_EXECUTABLE}" -sdk macosx metal "-std=${NOVA_METAL_STD}"
                -c "${base}.metal" -o "${base}.air"
            DEPENDS "${PROJECT_SOURCE_DIR}/render/shaders/model.hlsl" ${_nova_shader_extra_deps}
                "${NOVA_DXC_EXECUTABLE}" "${NOVA_SPIRV_CROSS_EXECUTABLE}" VERBATIM)
        add_custom_command(OUTPUT "${output}"
            COMMAND "${NOVA_XCRUN_EXECUTABLE}" -sdk macosx metallib
                "${SHADER_OUTPUT_DIR}/${kind}.vert.air" "${base}.air" -o "${output}"
            DEPENDS "${SHADER_OUTPUT_DIR}/${kind}.vert.air" "${base}.air" VERBATIM)
        list(APPEND SHADER_OUTPUTS "${output}")
    endforeach()
endif()
add_custom_target(triangle_shaders DEPENDS ${SHADER_OUTPUTS})
message(STATUS "Shader DXC: ${NOVA_DXC_EXECUTABLE}")

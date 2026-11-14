option(NOVA_BUILD_SKINNING_PROBE "Build the optional Metal GPU skinning readback probe" OFF)
if(NOVA_BUILD_SKINNING_PROBE AND APPLE)
    set(_skin_probe_dir "${CMAKE_CURRENT_BINARY_DIR}/skin-probe")
    set(_skin_probe_lib "${_skin_probe_dir}/skinning.metallib")
    add_custom_command(OUTPUT "${_skin_probe_lib}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_skin_probe_dir}"
        COMMAND "${NOVA_DXC_EXECUTABLE}" -spirv -fspv-target-env=vulkan1.0 -T cs_6_0 -E SkinningCSMain "${PROJECT_SOURCE_DIR}/render/shaders/model.hlsl" -Fo "${_skin_probe_dir}/skinning.spv"
        COMMAND "${NOVA_SPIRV_CROSS_EXECUTABLE}" "${_skin_probe_dir}/skinning.spv" --msl --msl-version 20100 --msl-decoration-binding --output "${_skin_probe_dir}/skinning.metal"
        COMMAND "${NOVA_XCRUN_EXECUTABLE}" -sdk macosx metal -std=macos-metal2.1 -c "${_skin_probe_dir}/skinning.metal" -o "${_skin_probe_dir}/skinning.air"
        COMMAND "${NOVA_XCRUN_EXECUTABLE}" -sdk macosx metallib "${_skin_probe_dir}/skinning.air" -o "${_skin_probe_lib}"
        DEPENDS "${PROJECT_SOURCE_DIR}/render/shaders/model.hlsl" VERBATIM)
    add_custom_target(nova_skin_probe_shader DEPENDS "${_skin_probe_lib}")
    add_executable(nova_skinning_probe tools/gpu_skinning/Metal.mm)
    target_link_libraries(nova_skinning_probe PRIVATE Nova::Render)
    target_include_directories(nova_skinning_probe PRIVATE render/src tests "${NOVA_TINYGLTF_INCLUDE_DIR}" "${NOVA_GLM_INCLUDE_DIR}")
    target_compile_options(nova_skinning_probe PRIVATE -fobjc-arc)
    add_dependencies(nova_skinning_probe nova_skin_probe_shader)
    if(BUILD_TESTING)
        add_test(NAME skinning-gpu-metal COMMAND nova_skinning_probe "${PROJECT_SOURCE_DIR}/assets/SkinningRobot/SkinningRobot.glb" "${_skin_probe_lib}")
        set_tests_properties(skinning-gpu-metal PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1" SKIP_RETURN_CODE 77 TIMEOUT 120)
    endif()
endif()

add_library(nova_android_triangle SHARED ../mobile/main.cpp)
set_target_properties(nova_android_triangle PROPERTIES OUTPUT_NAME main)
target_link_libraries(nova_android_triangle PRIVATE Nova::Render SDL3::SDL3)

add_library(nova_android_helmet SHARED ../mobile/main.cpp)
set_target_properties(nova_android_helmet PROPERTIES
    OUTPUT_NAME main
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/helmet")
target_link_libraries(nova_android_helmet PRIVATE Nova::Render SDL3::SDL3)
target_compile_definitions(nova_android_helmet PRIVATE NOVA_MOBILE_HELMET)

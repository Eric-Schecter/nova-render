add_library(nova_android_triangle SHARED ../mobile/main.cpp)
set_target_properties(nova_android_triangle PROPERTIES OUTPUT_NAME main)
target_link_libraries(nova_android_triangle PRIVATE Nova::Render SDL3::SDL3)

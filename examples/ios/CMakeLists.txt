add_executable(nova_ios_triangle MACOSX_BUNDLE main.cpp)
target_link_libraries(nova_ios_triangle PRIVATE Nova::Render SDL3::SDL3)
set(NOVA_IOS_BUNDLE_PLATFORM iPhoneOS)
if(CMAKE_OSX_SYSROOT MATCHES "iphonesimulator|iPhoneSimulator")
    set(NOVA_IOS_BUNDLE_PLATFORM iPhoneSimulator)
endif()
set_target_properties(nova_ios_triangle PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.nova.render.ios-triangle"
    MACOSX_BUNDLE_BUNDLE_NAME "Nova Triangle"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")

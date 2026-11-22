set(NOVA_IOS_BUNDLE_PLATFORM iPhoneOS)
if(CMAKE_OSX_SYSROOT MATCHES "iphonesimulator|iPhoneSimulator")
    set(NOVA_IOS_BUNDLE_PLATFORM iPhoneSimulator)
endif()
function(nova_ios_example name title)
    add_executable(nova_ios_${name} MACOSX_BUNDLE ../mobile/main.cpp)
    target_link_libraries(nova_ios_${name} PRIVATE Nova::Render SDL3::SDL3)
    set_target_properties(nova_ios_${name} PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.nova.render.ios-${name}"
        MACOSX_BUNDLE_BUNDLE_NAME "${title}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")
endfunction()

nova_ios_example(triangle "Nova Triangle")
nova_ios_example(helmet "Nova Damaged Helmet")
set(helmet "${PROJECT_SOURCE_DIR}/assets/DamagedHelmet/DamagedHelmet.glb")
set_source_files_properties("${helmet}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(nova_ios_helmet PRIVATE "${helmet}")
target_compile_definitions(nova_ios_helmet PRIVATE NOVA_MOBILE_HELMET)

# Local artifacts only. CPack never uploads or publishes a package.
set(CPACK_PACKAGE_NAME nova-sdk)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_GENERATOR "TGZ;ZIP")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
configure_file("${PROJECT_SOURCE_DIR}/cmake/SDKPackageConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SDKPackageConfig.cmake" @ONLY)
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/SDKPackageConfig.cmake")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/sdk-metadata/$<CONFIG>/sdk.json" CONTENT
"{\n  \"name\": \"Nova\",\n  \"version\": \"${PROJECT_VERSION}\",\n  \"system\": \"${CMAKE_SYSTEM_NAME}\",\n  \"architecture\": \"${CMAKE_SYSTEM_PROCESSOR}\",\n  \"compiler\": \"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\",\n  \"configuration\": \"$<CONFIG>\",\n  \"linkage\": \"static\",\n  \"bundledDependencies\": \"${NOVA_BUNDLE_DEPENDENCIES}\",\n  \"vulkan\": \"${NOVA_ENABLE_VULKAN}\",\n  \"ui\": \"${NOVA_ENABLE_UI}\"\n}\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sdk-metadata/$<CONFIG>/sdk.json" DESTINATION share/Nova)
install(FILES "${PROJECT_SOURCE_DIR}/docs/SDK-README.md" DESTINATION . RENAME README.md)
include(CPack)

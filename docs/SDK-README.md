# Nova Render C++ SDK

This archive contains a platform-specific C++17 static library, public headers,
embedded shaders, CMake package files, bundled dependency libraries and notices.
See `share/Nova/sdk.json` for the version, architecture, compiler and build configuration.
Use the same configuration, compatible compiler ABI and Windows C++ runtime.
A Debug archive does not contain a Release build of Nova.

```cmake
find_package(Nova 0.1 CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE Nova::Render)
```

Configure your application with `-DCMAKE_PREFIX_PATH=/path/to/extracted-sdk`.
SDK consumers need a C++ compiler and CMake, but no Nova checkout, vcpkg,
DXC, SPIRV-Cross or Node.js. Do not use the vcpkg toolchain for this consumer.
GPU drivers and operating-system graphics/window services remain required.
Dynamic dependency DLLs, if present, must be distributed beside the application.
Sample models and environment maps are not bundled.

This is a local development SDK. No public distribution license has been assigned.
Third-party notices are under `share/Nova/licenses` and `share/nova/licenses`.

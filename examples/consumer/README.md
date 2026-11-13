# Installed Nova consumer

This is a standalone CMake project. It uses only Nova's installation prefix, without the renderer source tree, vcpkg toolchain, shader compiler or external shader files.

```sh
cmake -S examples/consumer -B build/consumer \
  -DCMAKE_PREFIX_PATH=/absolute/path/to/nova-install \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build/consumer
./build/consumer/nova_consumer
```

Pass `metal`, `vulkan` or `dx12` as the first argument to select a backend. The example renders 120 frames and exits. Platform GPU drivers and window-system runtime remain necessary. On Windows, use a matching architecture and MSVC runtime; dynamic GLFW packages also need their DLL beside the executable or on PATH.

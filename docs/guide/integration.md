# Writing an example

Use `render::Window` without GLFW headers or platform graphics calls. It draws the built-in triangle until you [load a model](../api/model).

## 1. Write main.cpp

```cpp
#include <render/Window.hpp>
#include <iostream>

int main() {
    try {
        render::WindowConfig config;
        config.title = "My Nova example";
        config.width = 800;
        config.height = 600;
        render::Window window(config);
        while (window.PollEvents()) {
            window.DrawFrame();
        }
        window.WaitIdle();
        return window.ValidationErrorCount() ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

Close the window or press Esc to exit. Window handles events, framebuffer synchronization, skipped-frame waiting and destruction order. Set `config.renderer.api` before construction to explicitly request a backend, such as Vulkan.

## 2. Link the library

Installed SDK:

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyExample LANGUAGES CXX)
find_package(Nova 0.8 CONFIG REQUIRED)
add_executable(my_example main.cpp)
target_link_libraries(my_example PRIVATE Nova::Render)
```

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/nova-install
cmake --build build
```

Inside the repository, add an executable after the library declaration and link `Nova::Render`; no separate shader-build dependency is needed. See `examples/consumer/`, [packages](./package), or lower-level [Renderer](../api/renderer) for an externally managed window.

## Performance indicators

Examples show completed-update FPS, latest Draw time, P95 of the last 60 non-initial draws, internal resolution/scale and sample count in a separate bar. Static web views render on demand; idle/hidden FPS is —. Animation/tracing keeps updating. Comparison reports whole-update cost plus each integrator's active budget; its dimensions represent all four images.

Draw includes CPU preparation and GPU waits, not pure GPU time. Native examples wait for completion to match browser timing, limiting CPU/GPU overlap; ordinary library calls do not add this synchronization. Web example controllers expose `getPerformanceStats()`; native examples share PerformancePanel.

Only AO has adaptive interaction quality. `ParameterPanelConfig.sceneTopInset` reserves native logical pixels for a bar and adjusts viewport/projection/internal tracing size; UI-disabled builds keep the original viewport. Other than integrator comparison, panels float at the top right and can collapse.

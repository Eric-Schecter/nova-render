# 接入 Renderer

## CMake

当前在同一个源码树中使用 `nova_render` target，尚未提供 `find_package(Nova)` 或安装导出包。

```cmake
add_executable(my_demo main.cpp)
target_link_libraries(my_demo PRIVATE nova_render glfw)
add_dependencies(my_demo triangle_shaders)
target_compile_definitions(my_demo PRIVATE
    GLFW_INCLUDE_NONE
    APP_SHADER_DIR="${SHADER_OUTPUT_DIR}")
```

该片段放在当前项目中定义 `nova_render` 和 `triangle_shaders` 之后。GLFW 也是示例直接调用的窗口库。

## 最小主循环

下面示例通过 RAII 保证异常时也按正确顺序销毁窗口。它绘制模块内置的三角形，并不提交自定义场景。

```cpp
#include "render/Renderer.hpp"
#include <GLFW/glfw3.h>
#include <iostream>
#include <memory>
#include <stdexcept>

struct GlfwContext {
    GlfwContext() {
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
    }
    ~GlfwContext() { glfwTerminate(); }
};

int main() {
    try {
        render::RendererConfig config;
        config.shaderDirectory = APP_SHADER_DIR;
        render::ResolveGraphicsAPI(config);
        GlfwContext glfw;
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
            glfwCreateWindow(800, 600, "Nova", nullptr, nullptr), glfwDestroyWindow);
        if (!window) throw std::runtime_error("Window creation failed");
        render::Renderer renderer(window.get(), config);
        while (!glfwWindowShouldClose(window.get())) {
            glfwPollEvents();
            if (glfwWindowShouldClose(window.get())) break;
            int width, height;
            glfwGetFramebufferSize(window.get(), &width, &height);
            renderer.Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
            if (renderer.DrawFrame() == render::FrameResult::Skipped)
                glfwWaitEventsTimeout(0.01);
        }
        renderer.WaitIdle();
        return renderer.ValidationErrorCount() ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

也可通过 framebuffer size callback 更新尺寸。窗口逻辑尺寸和像素尺寸在 Retina / 高 DPI 环境中可能不同。主循环须持续处理 GLFW 事件；Renderer 不拥有事件循环。

完整的 CLI 和缩放、最小化恢复验证用例位于 `examples/triangle/main.cpp`。

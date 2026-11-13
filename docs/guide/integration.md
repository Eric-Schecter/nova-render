# 编写示例

当前 Renderer 绘制内置三角形。新例子可以练习窗口、后端选择和主循环，暂不能提交自定义网格或场景。

## 1. 创建文件

在仓库中新增 `examples/basic/main.cpp`，填入下面的主循环。

## 2. 注册构建目标

在根 `CMakeLists.txt` 的 `nova_render` 和 `triangle_shaders` 定义之后添加：

```cmake
add_executable(nova_basic examples/basic/main.cpp)
target_link_libraries(nova_basic PRIVATE nova_render glfw)
add_dependencies(nova_basic triangle_shaders)
target_compile_definitions(nova_basic PRIVATE
    GLFW_INCLUDE_NONE)
```

## 3. 编写主循环

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
        // 留空使用库内 shader。
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

## 4. 构建与运行

使用已配置好的构建目录：

```sh
cmake -S . -B build
cmake --build build --target nova_basic
./build/nova_basic
```

Windows 使用相应目录，例如 `cmake --build build-windows-vulkan --target nova_basic`，然后运行该目录下的 `nova_basic.exe`。多配置生成器的可执行文件可能在 `Debug/` 等配置子目录中。

这个例子默认按平台选择后端。需要指定后端时，在创建 Renderer 前设置：

```cpp
config.api = render::GraphicsAPI::Vulkan;
```

先保证基础例子运行，再增加自己的窗口交互。以上写法适用于本仓库内的示例；独立应用见[作为依赖使用](./package)。

# 编写示例

使用 `render::Window` 创建窗口并绘制，无需包含 GLFW 头文件或调用平台 API。当前绘制内置三角形，尚未支持自定义网格。

## 1. 编写 main.cpp

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

关闭窗口或按 Esc 退出。窗口内部处理事件、framebuffer 尺寸同步、跳帧等待及销毁顺序。

指定后端时，在创建窗口前设置 `config.renderer.api`；例如 `render::GraphicsAPI::Vulkan`。

## 2. 链接库

安装包用户：

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyExample LANGUAGES CXX)
find_package(Nova 0.1 CONFIG REQUIRED)
add_executable(my_example main.cpp)
target_link_libraries(my_example PRIVATE Nova::Render)
```

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/nova-install
cmake --build build
```

仓库内新增示例时，在根 CMakeLists 的库定义之后添加 executable 并链接 `Nova::Render` 即可，不需要单独注册 shader 编译依赖。

完整独立项目见 `examples/consumer/`；安装步骤见[作为依赖使用](./package)。需要自行管理窗口时，仍可使用底层 [Renderer](../api/renderer) 接口。

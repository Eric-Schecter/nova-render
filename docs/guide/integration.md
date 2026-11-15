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

## 例子性能指标

所有例子在渲染结果上方独立显示实际更新 FPS、最近一次 Draw 耗时、最近 60 次绘制的 P95（排除首次资源准备）、内部像素尺寸、分辨率比例和采样数。静态网页按需绘制，待命或不可见时 FPS 显示「—」；动画和光追持续更新。积分器对比的顶栏统计四个视图一次更新的整体耗时，各算法保留自己的预算统计；顶栏尺寸为四幅图像的总内部尺寸，采样数为每个视图每轮的设置。

Draw 包含 CPU 准备和 GPU 完成等待，不是纯 GPU 时间。C++ 例子开启指标时等待当前帧完成，以与网页的完成口径一致；这会限制 CPU/GPU 重叠，指标用于观察例子，不替代 GPU profiler。普通库调用不增加这项同步。网页例子返回对象提供 `getPerformanceStats()`；原生例子通过共用的 `PerformancePanel` 管理显示与计时。

只有 AO 例子启用交互自适应画质；其余例子保持原渲染质量策略。原生 UI 可通过 `ParameterPanelConfig.sceneTopInset` 预留顶部逻辑像素，渲染视口、投影比例和光追内部尺寸相应调整；UI 关闭构建保持原视口。

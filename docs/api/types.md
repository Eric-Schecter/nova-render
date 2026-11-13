# 类型与配置

## GraphicsAPI

```cpp
enum class GraphicsAPI { Auto, Vulkan, D3D12, Metal };
```

`Auto` 固定选择 macOS 的 Metal、Windows 的 D3D12、Linux 的 Vulkan。初始化失败不自动切换后端。

## RendererConfig

```cpp
struct RendererConfig {
    GraphicsAPI api = GraphicsAPI::Auto;
    bool enableValidation = true;
    bool useWarp = false;
    std::filesystem::path shaderDirectory;
};
```

- `api`：请求的后端。
- `enableValidation`：请求 Vulkan / D3D12 校验层；Metal 使用环境变量 `MTL_DEBUG_LAYER=1`。
- `useWarp`：显式选择 D3D12 CPU 软件渲染，仅对 D3D12 有效。
- `shaderDirectory`：生成的二进制 shader 目录，留空使用内置 shader；非空时严格从指定目录加载，建议传绝对路径。

资源文件：Vulkan 使用 `triangle.vert.spv` / `triangle.frag.spv`，D3D12 使用 `triangle.vert.dxil` / `triangle.frag.dxil`，Metal 使用 `triangle.metallib`。

## FrameResult

```cpp
enum class FrameResult { Rendered, Skipped };
```

`Rendered` 表示提交并安排呈现；`Skipped` 表示本次没有提交可计数的渲染帧。跳帧后继续处理窗口事件。

模型模式还使用 `model.vert.spv` / `model.frag.spv`、`model.vert.dxil` / `model.frag.dxil` 或 `model.metallib`，均默认内置。天空盒使用对应后端的 `skybox` shader，同样由 `model.hlsl` 生成并内置。显式指定外部目录时需提供用到的全部 shader。

## DirectionalLight

主方向光配置：`direction` 为表面指向光源的世界坐标向量，`color` 为线性 RGB，`intensity` 为强度倍率。C++ 与网页字段、默认值和范围一致。详见[模型与光照](./model#光照)。

## EnvironmentSettings

环境旋转、照明强度、背景亮度和显隐，C++ 与网页字段一致。默认值与范围见[天空盒与环境光](./environment#参数)。

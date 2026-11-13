# 后端与校验

## 支持与选择

- macOS：原生 Metal；启用 Vulkan 构建时也可通过 MoltenVK 使用 Vulkan。
- Windows：原生 D3D12；启用 Vulkan 构建时可选择 Vulkan。
- Linux：Vulkan。

`Auto` 是固定平台映射，不是探测后回退。shader、设备或首帧失败会报告原后端错误。`useWarp` 只能用于解析后的 D3D12。

## 校验环境

Vulkan 需要安装 Khronos Validation Layer。macOS 可通过 `VK_LAYER_PATH` 指向 Vulkan SDK 的 `share/vulkan/explicit_layer.d`。MoltenVK、Loader 和 ICD 属于运行环境。

D3D12 Debug Layer 需要 Windows Graphics Tools。设备支持 D3D12 不等于支持 DXIL：当前离线 shader 要求 Shader Model 6.0。

Metal API Validation 通过环境变量开启：

```sh
MTL_DEBUG_LAYER=1 ./build/nova_triangle.app/Contents/MacOS/nova_triangle --api metal
```

读取错误计数时，请同时核对实际设备和校验层日志。详见 [ValidationErrorCount](../api/renderer#validationerrorcount)。

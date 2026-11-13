---
layout: home
hero:
  name: Nova Render
  text: 从一个三角形开始
  tagline: C++17 渲染模块 · Vulkan / D3D12 / Metal · 一份 HLSL
  actions:
    - theme: brand
      text: 快速开始
      link: /guide/quick-start
    - theme: alt
      text: API 参考
      link: /api/
features:
  - title: 清晰的生命周期
    details: 应用管理窗口与事件，Renderer 管理 GPU 资源、尺寸变化和帧提交。
  - title: 明确的后端选择
    details: Auto 固定映射平台原生 API，显式选择和初始化失败均不会静默切换。
  - title: 离线 shader
    details: 维护单一 HLSL，构建时生成各后端二进制，运行时直接加载。
---

当前版本是最小固定三角形渲染器，适合学习、验证后端和逐步完善接口。它尚未提供通用场景、网格或材质 API。

文档以当前公共头文件 `render/Renderer.hpp` 为准。先阅读[接入指南](./guide/integration)，或查看[已验证范围](./guide/validation)。

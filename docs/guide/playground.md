# HLSL 即时演示

左侧编辑三角形的 HLSL，点击运行，右侧使用 WebGPU 绘制。可先修改 `colors` 或 `positions` 数组；入口保持 `VSMain` / `PSMain`。错误会显示在下方。

<ClientOnly><ShaderPlayground /></ClientOnly>

## 本地使用

```sh
npm ci
npm run docs:dev
```

需要已有 DXC；macOS 自动查找 Vulkan SDK，也可通过 `NOVA_DXC_EXECUTABLE` 指定。页面固定使用本机 `5173` 端口的编译服务，不支持 include。Naga WASI 工具随 npm 依赖安装，无需 Rust。

链路为 HLSL → DXC → SPIR-V → Naga → WGSL。保留原 HLSL 坐标空间，避免 Y 轴重复翻转。这是 WebGPU 演示，不是运行当前 C++ Renderer，也不能代替原生后端验证。

## 静态预览

`npm run docs:build` 会先生成 WGSL，静态网站无需编译服务器即可显示默认三角形。静态部署时编辑和运行按钮禁用；任意 HLSL 在线编译服务尚未提供。WebGPU 需要支持的浏览器和 HTTPS 或 localhost。

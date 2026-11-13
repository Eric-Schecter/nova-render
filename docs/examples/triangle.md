# 三角形

上方代码区展示下方预览实际执行的 `web/examples/triangle.js`，包含创建渲染器、加载 shader、设置背景和绘制。示例只调用 Nova API，WebGPU 细节封装在内部。

<ClientOnly><ShaderPlayground /></ClientOnly>

## 怎么使用

- **JavaScript 示例**：查看实际运行源码；点击运行重新执行。
- **背景色**：调用 `setClearColor()` 改变画面，可在静态网站使用。
- **HLSL shader**：本地可编辑位置和颜色，点击运行重新编译；静态网站仅展示源码。
- **重置**：恢复默认 shader 和背景。
- **隐藏预览 / 显示预览**：收起或展开画面，代码保持不变。隐藏时仍可编译，展开后重新绘制；错误信息始终显示在工具栏下方。

这是 Web 适配层示例，不是将 C++ Renderer 直接运行在浏览器里。[Web API 说明](../api/web)。

## 本地预览

```sh
npm ci
npm run docs:dev
```

需要已有 DXC；macOS 自动查找 Vulkan SDK，也可用 `NOVA_DXC_EXECUTABLE` 指定。HLSL 编译服务仅接受本机 `5173` 端口来源，不支持 include。链路为 HLSL → DXC → SPIR-V → Naga → WGSL。

`npm run docs:build` 生成静态资源。静态网站可执行同一份 JavaScript 示例并切换背景，修改 HLSL 仍需本地服务。浏览器须支持 WebGPU，并通过 HTTPS 或 localhost 访问。

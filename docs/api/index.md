# API 总览

公共头文件为 `render/Renderer.hpp`，所有接口位于 `render` 命名空间。当前支持内置三角形和单材质 GLB 模型（含骨骼动画），模型加载与相机控制使用封装接口。

- [模型与相机](./model)：GLB 加载、OrbitCamera 和共享 HLSL 材质。
- [骨骼动画](./animation)：片段、播放状态、时间与蒙皮。
- [天空盒与环境光](./environment)：HDR 天空盒、环境照明与资源加载。
- [参数面板](./ui)：预览区域内的可选 UI。
- [Nova Web](./web)：浏览器示例的封装接口。
- [Window](./window)：推荐的窗口与渲染入口，隐藏 GLFW 调用。
- [类型与配置](./types)：`GraphicsAPI`、`RendererConfig`、`FrameResult`。
- [后端查询与选择](./selection)：`GraphicsAPIName`、`IsGraphicsAPIAvailable`、`ResolveGraphicsAPI`。
- [Renderer](./renderer)：初始化、尺寸变化、绘制、等待与错误计数。

接口描述对应当前源码。内部 `detail::Backend` 和各平台后端不属于公共 API。当前不承诺跨版本稳定 ABI。

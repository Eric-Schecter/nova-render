# API 总览

公共头文件为 `render/Renderer.hpp`，所有接口位于 `render` 命名空间。当前版本是绘制固定三角形的最小渲染模块，尚无网格、材质、纹理或场景提交接口。

- [类型与配置](./types)：`GraphicsAPI`、`RendererConfig`、`FrameResult`。
- [后端查询与选择](./selection)：`GraphicsAPIName`、`IsGraphicsAPIAvailable`、`ResolveGraphicsAPI`。
- [Renderer](./renderer)：初始化、尺寸变化、绘制、等待与错误计数。

接口描述对应当前源码。内部 `detail::Backend` 和各平台后端不属于公共 API。当前没有稳定 ABI 或安装包承诺。

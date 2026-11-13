# Nova Web

浏览器适配层位于 `web/src/Nova.js`，当前为项目内模块，尚未发布 npm 包。与桌面接口共享生命周期概念，调用方式遵循 JavaScript 异步习惯。

## 使用

```js
import * as Nova from '../src/Nova.js'

const renderer = await Nova.createRenderer({ canvas })
try {
  await renderer.setShader(shader)
  renderer.setClearColor([0.025, 0.035, 0.055, 1])
  await renderer.drawFrame()
  // 保留 renderer 可继续绘制；页面退出时再释放。
} catch (error) {
  console.error(error)
} finally {
  renderer.dispose()
}
```

完整可运行示例位于 `web/examples/triangle.js`，文档页面直接调用这个文件，并展示其原始源码。

## 方法

模型模式新增 `loadModel(url)` 和 `setCamera(camera)`，见[模型与相机](./model)。

相机交互用 `enableOrbitControls()` 启用拖动旋转和滚轮缩放，`disableOrbitControls()` 关闭；`getCamera()` 读取当前相机副本。控件自动重绘，事件随 `dispose()` 清理。

- `createRenderer({canvas})`：创建 WebGPU 适配器、设备和画布上下文，失败时 Promise 拒绝。
- `setShader(shader)`：加载 `{source, vert, frag}` 预编译包；仅传 `{source}` 时请求本地 HLSL 编译服务。入口固定为 `VSMain` / `PSMain`。
- `setClearColor([r,g,b,a])`：设置背景，分量在 0～1 之间。
- `drawFrame()`：同步 canvas 像素尺寸并提交三角形；返回 `rendered` 或零尺寸时的 `skipped`。
- `waitIdle()`：等待提交的 GPU 工作。
- `dispose()`：释放设备和画布，允许重复调用；释放后不能继续绘制。

异步方法需要 `await`，同一实例上的操作须串行执行。支持三角形和静态单材质 GLB，不是通用场景 API。编译、管线和设备丢失错误会向调用方报告。

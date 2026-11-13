# 天空盒与环境光

加载一次环境资源，同时用于天空盒背景和 PBR 环境光。原生与网页使用相同的参数、预处理资源和 HLSL。

## C++

```cpp
window.LoadEnvironment("assets/Environment/venice_sunset.nenv");
render::EnvironmentSettings environment;
environment.rotation = 30;
environment.intensity = 1;
environment.backgroundIntensity = 1;
environment.showBackground = true;
window.SetEnvironment(environment);
// window.GetEnvironment() 返回副本，Renderer 同样提供这三个方法。
```

模型需要 `SetLighting(render::LightingMode::PBR)` 才接收环境光。可不加载模型，只绘制天空盒；天空盒不随相机距离变化。

## 网页

```js
await renderer.loadEnvironment('/generated/venice_sunset.nenv')
renderer.setEnvironment({ rotation: 30, intensity: 1, backgroundIntensity: 1, showBackground: true })
await renderer.drawFrame()
// getEnvironment() 返回副本，修改后调用 setEnvironment()。
```

`setEnvironment()` 接收完整配置，可先读取 `getEnvironment()` 再修改。加载失败保留原有环境；程序修改参数后需重绘，参数面板自动重绘。

## 参数

- `rotation`：绕世界 Y 轴旋转，单位为度，范围 −180～180，默认 0；背景与反射保持一致。
- `intensity`：模型的环境光倍率，范围 0～16，默认 1。
- `backgroundIntensity`：天空盒亮度倍率，范围 0～16，默认 1，不改变模型照明。
- `showBackground`：默认 `true`；关闭只隐藏天空盒，模型仍接收环境光。

数值须有限，无效设置报错并保留原配置。相机 `exposure` 同时影响天空盒与 PBR 模型。启用环境后，用 HDR 环境替代默认摄影棚辅助光，主方向光仍可叠加；第四个例子默认将主光强度设为 0。

## 资源

运行时读取离线准备的 `.nenv`，包含线性 HDR 立方体、GGX 预过滤 mip 和漫反射球谐系数；镜面反射采用分离求和近似。当前不包含阴影或动态环境捕获。

可选离线工具 `nova_environment_bake` 将 Radiance HDR 转为此内部格式，构建与命令见 `assets/Environment/README.md`。工具复用现有 stb / GLM，默认不构建；运行机器无需安装 HDR 解码器、DXC 或预处理工具。分发示例需同时携带 GLB 和 `.nenv`。

完整调用见[第四个例子](../examples/environment)，环境资源来源见页脚的资源声明。

第四例可切换 Venice Sunset、Kloppenheim 06 与 Studio Small 09。自定义资源列表见[参数面板](./ui#模型与环境选择)。

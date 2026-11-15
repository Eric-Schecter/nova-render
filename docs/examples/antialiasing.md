# 抗锯齿

程序化测试台包含细栏杆、斜边、透视棋盘和运动轮辐，无需模型资源。用几何边缘观察 MSAA，用棋盘细节观察 SSAA，用慢速运动观察 TAA 的闪烁与历史处理。

可选 **Off、FXAA、SMAA 1x、MSAA 4x、SSAA 2×2、TAA**。分割线左侧为 Off，右侧为所选算法；暂停运动并开启 4× / 8× 放大，便于查看像素差异。性能条独立位于画面上方，参数在下方，不自动降低画质。

具体实现与范围见 [抗锯齿接口](../api/antialiasing)。

<ClientOnly><AntiAliasingPlayground /></ClientOnly>

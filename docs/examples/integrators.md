# 积分器对比

PT、BDPT、光子映射与 MLT使用同一相机以两行两列渲染。默认使用玻璃焦散场景，关闭环境与方向光、降噪；每种方法达到预算后暂停。

在面板选择固定采样数或固定时间，再用“重置累计”重跑。顶部显示各自 spp（MLT 为 mutations/pixel）与实测耗时；相同 spp 不代表相同工作量，光子映射另有每轮光子预算，MLT 还有 bootstrap 与链初始化开销。拖动相机会重新开始。四个画面在窄屏也保持两行两列，参数面板独立放在下方。可切换 Weekend Spheres 多球场景。时间按各方法的活跃渲染时间累计，不含等待其他栏的时间。

<ClientOnly><ModelPlayground comparison /></ClientOnly>

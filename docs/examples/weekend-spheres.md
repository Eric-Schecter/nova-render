# 多球场景

《Ray Tracing in One Weekend》风格的随机球场景：134 个漫反射、金属及玻璃球，使用共享 HLSL 和现有 Nova API。默认开启光追和 HDR 环境，可切换光栅；光栅对玻璃采用不透明回退。

积分器对比页面也可在模型菜单选择 **Weekend Spheres**。对比模式统一使用场景中的面积光，不使用 HDR 环境。此场景为三角网格，未实现书中的解析球相交或景深。

<ClientOnly><ModelPlayground weekend /></ClientOnly>

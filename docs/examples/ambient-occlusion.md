# 环境遮蔽 AO

Intel Sponza 轻量白模，约 **4.76 MB**、25 万三角形。移除了纹理与材质差异，保留柱脚、拱廊和墙角以观察遮蔽。

切换 **SSAO、HBAO、MSAO、RTAO** 或 Off，可查看合成、仅 AO、深度与法线；参数面板位于画面下方。RTAO 累积到 32 帧后暂停，其他算法按需更新。拖动旋转、滚轮缩放，也可切换柱廊、门廊和总览视角。

算法与参数见 [AO 接口](../api/ambient-occlusion)。

<ClientOnly><ModelPlayground ao /></ClientOnly>

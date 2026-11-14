# Skinning Robot

Nova 自行生成的骨骼动画演示资源，含 11 个关节、180 个三角形、单材质与内嵌 PNG，提供 2 秒的 Walk 和 Wave 动画。肘、膝附近的顶点使用双关节混合权重，其余为刚性骨段。

使用标准 glTF 2.0 skins、JOINTS_0、WEIGHTS_0、inverseBindMatrices 与 LINEAR 四元数旋转通道，不依赖扩展、外部模型或动画库。几何、权重、关键帧和贴图均由项目脚本构建。

重建：`node scripts/generate-skinning-demo.mjs`。

示例入口：`nova_animation assets/SkinningRobot/SkinningRobot.glb`，网页为“05 · 骨骼动画”。共享 HLSL GPU 蒙皮、每帧仅更新骨骼/节点矩阵，动画模型暂使用光栅渲染。

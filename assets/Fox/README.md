# Fox

第五个骨骼动画例子的默认模型，来自用户指定的 Khronos 归档仓库：

- [模型目录](https://github.com/KhronosGroupArchives/glTF-Sample-Models/tree/main/2.0/Fox)
- [原始 GLB](https://raw.githubusercontent.com/KhronosGroupArchives/glTF-Sample-Models/main/2.0/Fox/glTF-Binary/Fox.glb)
- 24 个关节、576 个三角形；Survey、Walk、Run 三个 LINEAR 动画片段。
- SHA-256：`d97044e701822bac5a62696459b27d7b375aada5de8574ed4362edbba94771f7`

GLB 文件未修改。原文件是非索引三角形且未提供 NORMAL；Nova 加载时展开顶点并生成绑定法线，光栅着色时使用变形后几何导数恢复平面法线。继续使用共享 HLSL GPU 蒙皮。

## 来源与许可

- 原始低多边形 Fox：PixelMannen，[CC0](https://creativecommons.org/publicdomain/zero/1.0/)。
- 绑定与动画：tomkranis，[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)。
- glTF 转换：AsoboStudio 与 scurest。

完整上游原文保存在 `UPSTREAM.md`。模型许可独立于 Nova 代码，分发时请保留这些署名、来源与许可链接。

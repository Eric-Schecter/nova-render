# 资源声明

## Damaged Helmet

用于“模型加载”“光照”和“天空盒与环境光”示例，来自 [Khronos glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DamagedHelmet)。

- © 2018 ctxwing：重建与 glTF 转换，采用 [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/legalcode)。
- © 2016 theblueturtle_：早期模型，采用 [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/legalcode)。

上游版本从早期 glTF 2.0 草案导入 Blender 后重新导出，以兼容正式规范。Nova 使用的 GLB 文件未作修改；展示时在渲染器内部居中并归一化模型尺寸。

模型许可与 Nova 代码许可独立。仓库中的 `assets/DamagedHelmet/UPSTREAM.md` 保留上游原始署名与许可信息，`assets/DamagedHelmet/README.md` 记录来源和文件校验值。分发模型时请一并保留这些声明。

## Dear ImGui

原生可选参数 UI 使用 [Dear ImGui](https://github.com/ocornut/imgui)，© Omar Cornut and Dear ImGui contributors，采用 [MIT License](https://github.com/ocornut/imgui/blob/master/LICENSE.txt)。完整许可随安装包保存在 `share/imgui/copyright`。网页 UI 使用 Nova 的 HTML/CSS 实现。

## Venice Sunset

第四个例子的 HDR 环境来自 [Poly Haven · Venice Sunset](https://polyhaven.com/a/venice_sunset)，作者 Greg Zaal，采用 [CC0](https://polyhaven.com/license)。原始 HDR 未修改；Nova 离线生成用于背景与照明的预过滤立方体和漫反射系数。来源、校验值及重建命令保存在 `assets/Environment/README.md`。

## Avocado 与 BoomBox

来自 Khronos glTF Sample Assets 的 [Avocado](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Avocado) 与 [BoomBox](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/BoomBox)，© 2017 Microsoft，采用 [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/legalcode)。原始 GLB 未修改；上游署名与校验值保存在 `assets/Avocado/` 和 `assets/BoomBox/`。

## 其他 HDR 环境

[Studio Small 09](https://polyhaven.com/a/studio_small_09)（Sergej Majboroda）与 [Kloppenheim 06](https://polyhaven.com/a/kloppenheim_06)（Greg Zaal）来自 Poly Haven，采用 [CC0](https://polyhaven.com/license)。原始 HDR 未修改；Nova 生成预过滤资源，记录见 `assets/Environment/README.md`。

## Cornell Box

康奈尔盒子风格示意场景由 Nova 的 `scripts/generate-cornell-box.mjs` 生成，几何和两张纹理图集均在项目内构建，未使用外部模型素材。它不是 Cornell 实验室测量场景的复刻。资源说明见仓库 `assets/CornellBox/README.md`。

## Skinning Robot

GPU 蒙皮回归测试用的小机器人、关节、权重、两个动画片段及 PNG 图集均由 Nova 的 `scripts/generate-skinning-demo.mjs` 生成，未使用外部模型或动画素材。说明保存在 `assets/SkinningRobot/README.md`。


## Fox

第五个骨骼动画例子的默认模型来自 [Khronos glTF Sample Models · Fox](https://github.com/KhronosGroupArchives/glTF-Sample-Models/tree/main/2.0/Fox)。原始低多边形模型由 PixelMannen 提供，采用 [CC0](https://creativecommons.org/publicdomain/zero/1.0/)；绑定与动画由 tomkranis 提供，采用 [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)，glTF 转换由 AsoboStudio 与 scurest 完成。Nova 未修改 GLB 文件，来源、校验值和上游原文保存在 `assets/Fox/`。

# Cornell Caustics

Nova 原创生成的玻璃焦散场景：小面积顶灯、悬空闭合玻璃球与浅色地面。生成命令：

```sh
node scripts/generate-cornell-box.mjs CornellCaustics
```

玻璃使用 KHR_materials_transmission factor=1、KHR_materials_ior=1.5，粗糙度约 0.126。建议正面相机、8 次反弹、曝光 3，关闭环境光、方向光与降噪；光子初始半径建议 0.1。仅光追支持折射；光栅保留不透明材质回退。不同材质共用核心 PBR 因子和纹理图集，仅透射率与 IOR 分开。

当前为粗糙介质表面模型：空气/玻璃界面、Fresnel、Snell 折射及全反射；不含吸收、色散、嵌套介质与透射纹理。未使用外部几何或纹理。

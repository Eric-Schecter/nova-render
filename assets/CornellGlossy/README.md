# CornellGlossy

粗糙金属反射：较小顶灯与粗糙金属球，观察地面反射焦散和金属反射路径。

由 Nova 的 `scripts/generate-cornell-box.mjs` 原创生成，未下载外部几何或纹理，不是测量数据复刻。复现：

```sh
node scripts/generate-cornell-box.mjs CornellGlossy
```

建议 yaw=0、pitch=0、distance=3.6、exposure=16；比较积分器时关闭方向光、环境照明和降噪。金属球使用几何法线作为着色法线，以隔离三角化平滑法线的伴随修正方差；材质为粗糙 GGX 金属反射，未使用玻璃折射。

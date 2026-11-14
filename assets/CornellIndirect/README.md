# CornellIndirect

遮挡光源的间接照明：顶灯下增加漫反射挡板，使房间下部主要依靠多次反弹照明。

由 Nova 的 `scripts/generate-cornell-box.mjs` 原创生成，未下载外部几何或纹理，不是测量数据复刻。复现：

```sh
node scripts/generate-cornell-box.mjs CornellIndirect
```

建议 yaw=0、pitch=0、distance=3.6、exposure=12；比较积分器时关闭方向光、环境照明和降噪。

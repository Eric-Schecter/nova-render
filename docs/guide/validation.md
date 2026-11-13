# 示例与验证状态

## 示例参数

- `--api auto|vulkan|metal|dx12`：选择后端。
- `--warp`：显式 D3D12 软件设备。
- `--frames N`：成功绘制 N 帧后退出，N 为正整数。
- `--resize-test`：20 / 40 帧调整尺寸，60 帧验证最小化、零尺寸跳帧和恢复。
- `--shader-dir PATH`：覆盖二进制资源目录。

```sh
ctest --test-dir build --output-on-failure
```

CTest 包含平台组合拒绝、Auto、可用后端、缺失 shader 和不回退检查。窗口测试需可交互的桌面或配有窗口管理器的测试显示环境。

## 已记录结果

以下是 HLSL 离线构建完成时的 ARM64 环境记录，不代表所有驱动或未来提交均已验证。

- macOS：完整构建 12 项、Metal-only 11 项通过；Metal API Validation 和 Vulkan Khronos 层启用。Metal 三角形方向、颜色已目视检查。
- Windows 11 ARM64 虚拟机：完整构建 9/12、DX12-only 8/11 通过。WARP 绘制和窗口检查通过；每种构建各 3 项普通 DX12 / Auto 失败，因为 Parallels 虚拟显卡仅支持 DXBC，不支持 DXIL。
- Windows Vulkan：ARM64 SwiftShader 功能检查通过，但环境没有 Khronos Validation Layer。
- Ubuntu ARM64：Lavapipe + X11/Openbox 11 项运行检查通过，另通过禁用 Vulkan 的配置拒绝检查；Khronos 层启用。构建工具使用隔离的 x64 DXC + QEMU，渲染器和 Lavapipe 为原生 ARM64。

合计 58 项平台检查中 52 项通过、6 项因虚拟 GPU 的 DXIL 限制失败。独立检查还验证了 shader 增量构建、语法错误阻断和 SPIR-V 合法性。

## 尚未完成

Windows 真机 SSH 访问已准备好，但真实 GPU 的 DX12 / Vulkan 验证暂缓；不能把连接成功视为渲染验证通过。尚未覆盖 x64 真机、Wayland、NVIDIA / AMD / Intel 实机驱动与性能。Linux 是隔离显示环境自动检查，并非登录桌面的目视验收。

本地详细记录保存在被 Git 忽略的 `build/validation/hlsl-*` 中。运行测试、API 校验、图像正确性和性能测试是不同层面的证据。

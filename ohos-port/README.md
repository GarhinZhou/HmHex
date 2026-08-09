# ImHex OpenHarmony 移植

将 ImHex v1.38.1 完整移植到 OpenHarmony / HarmonyOS 的平台工程。

## 架构

```
┌─────────────────────────────────────────────────────┐
│  HAP (imhex-hap/)                                   │
│  ┌───────────────────────────────────────────────┐  │
│  │ ArkTS 壳 (Index.ets)                          │  │
│  │   XComponent(libraryname="entry")             │  │
│  │   ├─ rawfile 插件部署 → 沙箱 ~/.local/share/  │  │
│  │   └─ libs/arm64-v8a/                          │  │
│  │      ├─ libentry.so   ← GUI + XComponent 入口 │  │
│  │      ├─ libimhex.so   ← ImHex 核心            │  │
│  │      └─ libc++_shared.so                      │  │
│  └───────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘

libentry.so 内部：
  xcomponent_entry.cpp   OH_NativeXComponent_* 回调（surface/输入）
  glfw_ohos.cpp          GLFW 兼容层（EGL + GLES3 + NativeWindow）
  main/gui/*             ImHex 窗口/初始化/渲染循环（原样复用）
```

## 已编译验证

- [x] libimhex.so（完整依赖：fmt/libwolv/PatternLanguage/imgui/lunasvg/mbedtls/magic/curl）
- [x] libentry.so（975K，导出 OH_NativeXComponent_* 符号）
- [x] 全部 9 个内置插件（builtin/disassembler/fonts/ui/hashes/decompress/diffing/remote/visualizers）
- [x] `imhex` 可执行文件本机运行通过（`--help` 完整启动流程）
- [ ] GUI 渲染（EGL 初始化）— 需真机验证

## 构建 HAP

本机（开发机）只有 NDK 部分 SDK，**缺 ArkTS 编译器与 hap-sign-tool**。
两步构建：

```sh
# 1) 本机：组装 native 产物（已完成，可重复执行）
./imhex-hap/scripts/build_hap.sh stage1

# 2) 有完整 DevEco Studio 工具链的机器上：
cd imhex-hap
hvigorw assembleHap --mode module -p product=default -p buildMode=release
# 产物: entry/build/default/outputs/default/entry-default-signed.hap
```

### 签名

- HAP 签名：DevEco 自动完成（自动签名模式）或 hap-sign-tool.jar
- **native .so 签名**（鸿蒙 PC 要求）：`binary-sign-tool sign -inFile xxx.so -outFile xxx_signed.so -keyAlias ... -appCertFile ... -profileFile ... -signAlg SHA256withECDSA -keystoreFile ...`
  （调试证书可由 DevEco 生成；签名后需 64KB 页对齐，参考 ohos-pip-autosign 的处理流程）

## 真机验证清单

1. 安装 HAP：`hdc install entry-default-signed.hap`
2. 启动应用 → 观察：
   - [ ] XComponent surface 创建（native OnSurfaceCreated 触发）
   - [ ] EGL 初始化成功（libentry.so 日志无 "EGL: eglInitialize failed"）
   - [ ] ImHex 启动画面出现
   - [ ] 主界面渲染（十六进制编辑器）
   - [ ] 鼠标移动/点击响应（XComponent 鼠标回调）
   - [ ] 键盘输入（焦点 + 按键）
   - [ ] 插件加载：`~/.local/share/imhex/plugins/` 下 9 个插件被加载
   - [ ] Tools → Decompiler 可用（反编译插件）
3. 问题排查：
   - `hdc shell "cat /data/app/el2/100/base/net.werwolv.imhex/files/imhex.log"` 查看日志
   - EGL 失败：确认设备图形栈（SCB/Rosen）正常
   - 插件未加载：检查 rawfile 部署是否成功（ArkTS hilog）

## 关键文件

| 路径 | 说明 |
|---|---|
| `ohos-port/glfw/glfw_ohos.cpp` | GLFW 兼容层（~1100 行，EGL/GLES3/NativeWindow/事件） |
| `ohos-port/entry/xcomponent_entry.cpp` | XComponent 入口 + 输入转发 + 沙箱路径 |
| `ohos-port/nfd_stub/nfd_stub.cpp` | NFD 文件对话框 stub（后续桥接鸿蒙 picker） |
| `imhex-hap/` | HAP 工程（ArkTS 壳 + 插件 rawfile + 构建脚本） |
| ImHex-1.38.1 源码改动 | 见 imhex-decompiler-plugin/README.md「本地修改记录」+ CMake 的 IMHEX_OHOS_PORT 分支 |

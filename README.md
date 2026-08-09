# ImHex for OpenHarmony (OHOS PC)

ImHex 逆向工程十六进制编辑器移植到 Harmony PC 的完整工程。
以单个 HAP 应用形式运行：ArkUI `XComponent` 承载由 GLFW 兼容层 + ImGui 驱动的
完整 ImHex GUI（含 9 个内置插件静态链接），通过 NAPI 桥接文件选择、保存、
剪贴板、置顶、拖放导入等系统能力。

## 仓库结构

```
ImHex-OHOS/
├── imhex-hap/                  # DevEco Studio 工程（entry 模块 = 全部 native 代码）
├── ImHex-1.38.1/               # ImHex 上游源码（v1.38.1，含 OHOS 移植修改）
├── ohos-port/                  # 移植层：glfw_ohos (EGL/GLES3/NativeWindow)、
│                               #   xcomponent_entry (NAPI 桥)、nfd_stub、magic stub
├── deps/                       # 第三方依赖源码 + 预编译 capstone（decompiler 插件用）
│   └── magic/                  # libmagic 声明头（magic.h，配合 magic_stub 使用）
└── imhex-decompiler-plugin/    # 反编译插件（Capstone 反汇编，独立 hexplug）
```

## 构建

### 前置条件

- DevEco Studio（HarmonyOS 5.0.2+ / SDK 6.1.0(23)，本工程 `compatibleSdkVersion` 6.1.0(23)）
- harmonybrew 安装的 OpenHarmony SDK `26.0.0.18_1`（默认路径
  `~/.harmonybrew/Cellar/ohos-sdk/26.0.0.18_1`，可用 `-DHARMONYBREW_SDK=...`
  覆盖）

### 步骤

1. 用 DevEco Studio 打开 `imhex-hap/`
2. **配置签名**：`File -> Project Structure -> Signing Configs`，
   勾选 *Automatically generate signature*（仓库不含任何签名材料）；
   然后把 `imhex-hap/build-profile.json5` 中 product 的
   `// "signingConfig": "default"` 注释恢复
3. `Sync` 后 `Run` 即可编译 native（CMake 全量编译 ImHex + 依赖并签名）
4. 设备端部署后，首启会自动把内置 patterns/资源部署到沙箱目录

> 说明：`imhex-hap/entry/CMakeLists.txt` 中的源码路径均为相对仓库根的相对路径，
> 仓库移动到任意位置均可直接编译；CMake 配置假定 **host == target**
> （在 OHOS PC 本机编译），交叉编译到其他形态设备需另行调整。

## 与上游 ImHex v1.38.1 的主要差异

- **构建**：静态链接 9 插件（沙箱 linker 拒绝从应用目录 dlopen）；libromfs 资源、
  freetype/curl/mbedtls/libssh2/zlib 全部随应用源码编译并整体签名
- **渲染/窗口**：GLFW 兼容层映射到 NativeWindow (EGL + GLES3)；窗口为无边框
  沉浸式（菜单栏内嵌系统三键）；DPI 缩放修正
- **输入**：鼠标/键盘/滚轮事件经 XComponent NAPI 转发并做坐标换算；
  Ctrl+C/V 等修饰键组合修正；拖放文件导入
- **系统能力桥接**：文件打开/保存走 DocumentViewPicker（fd 直传 native）、
  剪贴板写入、窗口置顶、退出应用、打开网页等 NAPI 回调
- **稳定性**：异常恢复（ErrorRecovery）防御、崩溃看门狗、帧状态自愈、
  视图窗口越界自动居中、自动备份时间串格式防御等
- **UI**：标题栏搜索框与命令面板对齐、工具栏 ID 冲突修复、默认 FPS 无限制等

## 已知限制

- 更新走应用商店（`imhex-updater` 不存在，检查更新仅提示）
- 剪贴板读取受系统签名限制，仅实现写入方向
- 插件无法动态加载，新插件需加入 `IMHEX_PLUGINS` 静态链接列表

## 许可与法律

本仓库是 **ImHex（WerWolv/ImHex）的非官方移植**，与 ImHex 上游项目及其
维护者无任何关联。ImHex 及本仓库内的修改（移植代码、HAP 工程、插件）以
**GPL-2.0-or-later** 许可发布（见仓库根 `LICENSE`，与上游 ImHex 的
`ImHex-1.38.1/LICENSE` 一致）。将本仓库公开托管即满足 GPL 的源代码提供义务。

自有的三个子项目（`ohos-port/`、`imhex-hap/`、`imhex-decompiler-plugin/`）
各自目录内均附有同一 `LICENSE` 文件。

第三方依赖（保留各自许可，见对应目录内 LICENSE/COPYING）：

| 组件 | 许可 |
|---|---|
| curl | MIT/X11 (`deps/curl-8.21.0/COPYING`) |
| mbedTLS | Apache-2.0 / GPL-2.0 (`deps/mbedtls-3.6.2/LICENSE`) |
| FreeType | FTL / GPL-2.0 (`deps/freetype-2.14.3/LICENSE.TXT`) |
| libssh2 | BSD-3 (`deps/libssh2-1.11.1/COPYING`) |
| zlib | zlib (`deps/zlib-1.3.1/LICENSE`) |
| CLI11 | BSD-3 (`deps/CLI11-2.4.2/LICENSE`) |
| Capstone（`deps/prefix/` 内预编译静态库） | BSD-3 (`deps/prefix/LICENSE.capstone`) |
| jthread | CC-BY-4.0 (`deps/jthread-master/README.md`) |

> 商标/名称：ImHex 名称与图标版权归其原作者；本移植仅为兼容性/可用性目的引用。
> 本项目全程通过 KimiCode + DeepseekV4flash正式版 完成移植，共计消耗1,187,853,807tokens，花费¥32.69CNY


# ImHex Decompiler Plugin

一个为 [ImHex](https://github.com/WerWolv/ImHex) v1.38.x 编写的反编译（反汇编）插件，基于 **Capstone** 引擎，
支持多种架构。安装后可在 ImHex 的 **Tools（工具）** 窗口中找到 **Decompiler** 工具。

## 功能

- 对当前打开文件中**选中的区域**（或指定地址范围）进行反汇编
- 支持架构：
  - AArch64 (ARM64)
  - ARM (32-bit)
  - ARM Thumb
  - x86 16/32/64-bit（支持 Intel / AT&T 语法切换）
  - RISC-V (64-bit)
- 结果以表格展示：地址 / 原始字节 / 助记符 / 操作数
- 调用（CALL）、跳转（JUMP）、返回（RET）指令以不同颜色高亮
- 跳转指令显示解析后的绝对目标地址；点击地址可跳转到 Hex 编辑器中的对应位置
- 反汇编在后台任务中执行，带进度条，不阻塞界面
- 支持一键复制反汇编结果到剪贴板
- 遇到无法解码的字节会以 `??` 占位并继续（不会中断）

## 构建

### 环境

- 构建机器：OpenHarmony（本仓库已配好完整工具链）
- 依赖：`clang`、`cmake`、`git`、`curl`、`python3`

### 步骤

```sh
# 1. 构建 ImHex SDK（libimhex + 依赖库）与 Capstone 静态库
cmake -S sdk-build -B build/sdk \
    -DCMAKE_BUILD_TYPE=Release \
    -DIMHEX_SKIP_IMGUI_BACKEND=ON \
    -DCMAKE_PREFIX_PATH="<repo>/deps/prefix;<home>/.harmonybrew"
cmake --build build/sdk -j$(nproc)

# 2. 产物
#    插件:   imhex-decompiler-plugin/dist/decompiler.hexplug
#    SDK:    build/sdk/libimhex/libimhex.so（插件链接依赖，部署时放在 ImHex 可找到的库路径下）
```

### 说明

- `sdk-build/CMakeLists.txt` 会从 `ImHex-1.38.1/`（官方源码，依赖已补齐）编译
  `libimhex` 及其全部依赖（fmt、libwolv、PatternLanguage、imgui、lunasvg、mbedtls 等）。
- 本平台使用的华为 OHOS clang 缺少 C++20 聚合括号初始化（P0960）支持，因此对
  `libcxx-ohos` 的 `__memory/construct_at.h` 打了一个花括号初始化补丁（见下文"本地修改"）。

## 安装

1. 将 `decompiler.hexplug` 复制到 ImHex 插件目录：
   - Linux: `~/.local/share/imhex/plugins/`
   - 也可以直接用命令行参数 `--plugins <目录>` 指定
2. 将 `build/sdk/libimhex/libimhex.so` 放置到 `libimhex.so` 可以被加载器找到的位置：
   - 与插件同目录，或 `~/.local/share/imhex/lib/`（ImHex 的 libraries 目录）
   - Linux 发行版的 ImHex 通常已自带 `libimhex.so`（系统路径），此时无需复制
3. 启动 ImHex，打开任意文件，选中一段字节，打开 Tools → Decompiler，点击 Disassemble。

## 使用

1. 在 ImHex 中打开一个二进制文件（ELF / raw bin 等），用鼠标框选要分析的区域
2. 打开 **Tools** 窗口（菜单 `Tools` → 列表中出现 "Decompiler"）
3. 选择 **Architecture**（如 AArch64），**Start address** 留空表示使用当前选中区域起点，
   **Size** 留空表示到文件末尾（或选中区域大小）
4. 点击 **Disassemble**，等待后台任务完成后查看结果表
5. 点击表中任意地址即可让 Hex 编辑器跳转到该位置

## 本地修改记录（相对 ImHex v1.38.1 源码）

在 OHOS 工具链下构建所需的最小改动，均不影响插件本身逻辑：

| 文件 | 修改 |
|---|---|
| `libcxx-ohos/.../__memory/construct_at.h` | 新增 `__construct_at_impl`：聚合用花括号、其余用括号构造（华为 clang 缺 P0960，且 narrowing 判定有 bug） |
| `libcxx-ohos/.../__memory/{unique_ptr,shared_ptr,allocator_traits,raw_storage_iterator}.h` | 直接括号构造点改走 `__construct_at_impl` |
| `libcxx-ohos/.../__memory/ranges_construct_at.h` | SFINAE 改用花括号 |
| `libcxx-ohos/.../__config` | 取消 `_LIBCPP_HAS_NO_INCOMPLETE_RANGES`（恢复 std::ranges） |
| `libcxx-ohos/.../ranges` | 恢复被华为整体注释的 `<ranges>` 头（改为 include `__ranges/*.h` 实现） |
| `libcxx-ohos/.../source_location`（新增） | 华为缺失的 C++20 `<source_location>` 头（header-only 实现，ABI 与上游一致） |
| `lib/external/pattern_language/lib/include/pl/pattern_language.hpp` | `Function` 增加普通构造函数 |
| `lib/external/libwolv/libs/utils/CMakeLists.txt`（两处） | `-Wno-c2y-extensions` 仅在 clang ≥ 16 时启用 |
| `lib/third_party/imgui/backend/CMakeLists.txt` | 新增 `IMHEX_SKIP_IMGUI_BACKEND` 开关（无 OpenGL 环境跳过 GLFW 后端） |
| `lib/third_party/imgui/CMakeLists.txt` | 编译定义循环跳过 INTERFACE 目标 |
| `lib/libimhex/include/hex/api/localization_manager.hpp` | `UnlocalizedString` 的比较改用 `compare()`（libcxx-ohos 的 string 无 `<=>`） |
| `lib/libimhex/include/hex/api/event_manager.hpp` | `E::Callback` 补 `typename` |
| `lib/libimhex/include/hex/providers/provider_data.hpp` | `views::values` 改用 `views::transform`（libcxx-ohos 无 elements_view） |
| `lib/libimhex/source/subcommands/subcommands.cpp` | `views::split` 改用 `views::lazy_split`，子范围改用循环拷贝 |
| `lib/libimhex/source/helpers/utils.cpp` | OHOS 上跳过 `dladdr`（OHOS 的 dlfcn.h 裁剪了 Dl_info/dladdr） |
| `lib/libimhex/include/GLFW/`（新增） | 从 glfw 3.3.9 拷贝的头文件（fs.cpp 仅需要头） |

## 文件结构

```
imhex-decompiler-plugin/
├── CMakeLists.txt          # 插件构建（作为 sdk-build 子工程）
├── README.md
├── source/
│   ├── plugin_decompiler.cpp   # IMHEX_PLUGIN_SETUP 入口，注册工具
│   ├── decompiler.hpp/.cpp     # Capstone 反汇编核心引擎
│   └── tool_decompiler.hpp/.cpp# 工具面板 UI
├── test/
│   └── decompiler_test.cpp     # 核心引擎单元测试（31 项断言，全部通过）
├── scripts/
│   └── package.sh              # 安装插件到 ImHex 插件目录
└── dist/                   # 构建产物（decompiler.hexplug）
```

## 测试

```sh
# 核心引擎单元测试（不依赖 ImHex，仅需 capstone 静态库）
clang++ -std=c++2b -I source -I ../deps/prefix/include \
    test/decompiler_test.cpp source/decompiler.cpp \
    ../deps/prefix/lib/libcapstone.a -o /tmp/decompiler_test
/tmp/decompiler_test   # 期望输出: ALL TESTS PASSED (0 failures)
```

## 兼容性

- 插件 API 对应 **ImHex 1.38.x**（`IMHEX_PLUGIN_SETUP`、`ContentRegistry::Tools`）。
- 插件静态链接 Capstone 与 libc++/libimhex 依赖，运行时仅要求宿主 ImHex 提供 `libimhex` 符号
  （ImHex 加载插件使用 `dlopen(RTLD_NOW | RTLD_GLOBAL)`）。
- 本构建产物为 **aarch64 Linux (OHOS)** ELF；如需部署到 x86_64 桌面，请用 x86_64 工具链
  按相同流程交叉编译（`CMakeLists.txt` 与源码无需改动）。

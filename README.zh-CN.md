# Nioh3 Plugins

[English](README.md) | **简体中文**

《仁王 3》的 Windows x64 原生插件集合，使用 C++23、CMake 和 MSVC 构建。各插件可独立使用，公共代码提供地址定位、hook、日志、配置读取及游戏数据结构定义。

## 插件

| 插件 | 功能 | 当前状态 |
| --- | --- | --- |
| [LooseFileLoader](plugins/LooseFileLoader) | 从磁盘加载模型、贴图等替换资源，无需重新打包游戏档案 | `1.3.0`，已针对游戏 `2.0.2.0` 适配并完成 Release 构建 |
| [UnlimitedTransmog](plugins/UnlimitedTransmog) | 解锁装备幻化，并放宽武士与忍者之间的幻化使用限制 | `1.0.4`，已针对游戏 `2.0.2.0` 适配并完成 Release 构建 |
| [LivingArtifactCutsceneAlways](plugins/LivingArtifactCutsceneAlways) | 调整神器变身演出的播放条件，使其始终播放 | 旧版实现，未纳入本次适配与构建验证 |
| [GameDumper](plugins/GameDumper) | 装备数据输出、模型与资源加载链路调试 | 研究用代码，未纳入本次适配与构建验证 |

游戏更新可能改变函数位置和数据布局。上述适配状态不代表整个仓库的所有插件或公共结构都兼容该版本；两个已适配插件已完成静态核对，游戏内效果仍需实际验证。

## 安装

本仓库包含插件源码和公共库，插件加载器需要另外准备。加载器应支持 [PluginAPI.h](common/include/PluginAPI.h) 中的接口，并调用插件导出的 `nioh3_plugin_initialize`。

1. 退出游戏，将需要使用的插件 DLL 放入游戏目录的 `plugins` 文件夹。
2. 将对应的 INI 模板复制到同一文件夹；升级时保留自己的配置。
3. 使用 LooseFileLoader 时，将资源替换文件放入游戏目录的 `mods` 文件夹。
4. 启动游戏，在 `logs` 文件夹检查插件初始化日志。

```text
Nioh3/
├── Nioh3.exe
├── plugins/
│   ├── LooseFileLoader.dll
│   ├── LooseFileLoader.ini
│   ├── UnlimitedTransmog.dll
│   └── UnlimitedTransmog.ini
├── mods/
│   ├── 0x1234ABCD.g1m
│   └── MyTextureMod/
│       └── 0x89ABCDEF.g1t
└── logs/
    ├── LooseFileLoader.log
    └── UnlimitedTransmog.log
```

移除插件时，退出游戏后移走对应 DLL 即可。配置与资源替换文件可按需保留。

## 使用与配置

### LooseFileLoader

配置模板：[LooseFileLoader.ini](plugins/LooseFileLoader/LooseFileLoader.ini)。

```ini
[LooseFileLoader]
EnableAssetLoadingLog=0
DisableStreamingLoading=0
EnableConsole=0
AssetLoggerFilters=ModelData,StreamingTexContext,TexContext
```

| 配置项 | 作用 |
| --- | --- |
| `EnableAssetLoadingLog` | 设为 `1` 时记录资源加载信息，便于查找需要替换的资源 |
| `DisableStreamingLoading` | 设为 `1` 时禁用流式贴图加载，供相关资源调试使用；通常保持 `0` |
| `EnableConsole` | 设为 `1` 时显示日志控制台 |
| `AssetLoggerFilters` | 限制记录的资源类型，使用逗号分隔且区分大小写；留空表示不按类型筛选 |

资源文件放置规则：

- 文件名主体必须是 **8 位十六进制资源 ID**，可带 `0x` 前缀，如 `1234ABCD.g1m` 或 `0x1234ABCD.g1m`。
- 只扫描 `mods` 根目录和它的**一级子目录**，不递归扫描更深层目录。
- 同一资源 ID 出现多份替换时，根目录文件优先；子目录按名称排序，先匹配的文件生效。冲突会写入日志。
- 扩展名不参与 ID 解析，但文件内容应与目标资源格式匹配。
- 替换索引在启动时建立，新增或移除资源文件后应重启游戏。

### UnlimitedTransmog

配置模板：[UnlimitedTransmog.ini](plugins/UnlimitedTransmog/UnlimitedTransmog.ini)。

```ini
[UnlimitedTransmog]
EnableUnlockAllTransmog=1
EnableSamuraiNinjaSharedTransmog=1
```

- `EnableUnlockAllTransmog`：解锁有效装备的幻化选项。
- `EnableSamuraiNinjaSharedTransmog`：放宽武士与忍者之间的幻化限制，不代表任意武器类型之间都能互换。

两个选项均可独立设置，`1` 为启用、`0` 为关闭；缺少配置时默认启用。修改后重启游戏。

## 从源码构建

### 环境

- Windows x64。
- Visual Studio 2022 / MSVC，安装 C++ 桌面开发工具及 Windows SDK，工具链需支持项目使用的 C++23 特性。
- CMake **3.21 或更新版本**。
- vcpkg，使用 `x64-windows-static` triplet。

[vcpkg.json](vcpkg.json) 声明了 `spdlog`、`fmt`、`zlib` 和 `nlohmann-json` 依赖；SafetyHook、LightningScanner 等源码已包含在 `common` 中。

### 先检查本机路径

根目录 [CMakeLists.txt](CMakeLists.txt) 当前包含本机绝对路径，首次构建前请按实际环境修改：

| 设置 | 当前路径 | 说明 |
| --- | --- | --- |
| vcpkg toolchain | `D:/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake` | 同时检查 `CMAKE_TOOLCHAIN_FILE` 和显式 `include(...)`，只传命令行 toolchain 参数不会替换后者 |
| 构建后复制目录 | `E:/SteamLibrary/steamapps/common/Nioh3/plugins` | 插件 DLL 构建成功后自动复制到这里，目录应提前存在 |
| Visual Studio 调试程序 | `H:/SteamLibrary/steamapps/common/Nioh3/Nioh3.exe` | 按需修改为实际游戏程序路径 |

**插件构建自带部署步骤**：若只想编译而不覆盖游戏中的 DLL，请先调整 `POST_BUILD` 复制步骤。游戏运行时 DLL 可能被占用，导致复制失败；编译成功与部署成功应分别检查。

### 配置与定向构建

以下 PowerShell 命令从仓库根目录执行：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

cmake --build build --config Release --target LooseFileLoader
cmake --build build --config Release --target UnlimitedTransmog
```

DLL 输出在 `build/bin/Release/`，公共静态库输出在 `build/lib/Release/`。当前构建脚本只自动复制 DLL，INI 模板需手动安装。

优先指定需要的 `--target`，避免全量构建研究用或尚未适配的插件。已有 `build` 目录时，请保持与其一致的生成器和架构。

## 开发与验证

```text
common/                         公共工具与游戏类型定义
plugins/LooseFileLoader/         资源覆盖插件、RDB 工具及测试
plugins/UnlimitedTransmog/       幻化插件及适配验证脚本
plugins/LivingArtifactCutsceneAlways/
plugins/GameDumper/              数据输出与资源链路研究
```

### UnlimitedTransmog 离线验证

脚本需要 Python、`pefile`、`capstone` 和自行准备的 `2.0.2.0` 解包 EXE：

```powershell
python -m pip install pefile capstone
python plugins/UnlimitedTransmog/analysis/verify_2_0_2.py "E:/SteamLibrary/steamapps/common/Nioh3/Nioh3.exe.unpacked.exe"
```

验证签名命中、调用目标、字段偏移、寄存器约定及装备类型映射。脚本只读文件，不运行或修改游戏，也不替代游戏内测试。

### LooseFileLoader RDB 工具测试

```powershell
cmake --build build --config Release --target LooseFileLoaderRdbToolTests
./build/bin/Release/LooseFileLoaderRdbToolTests.exe
```

测试需要自行准备 `plugins/LooseFileLoader/package` 下的 RDB 样本；样本未随当前 Git 仓库提交。测试会复制样本到临时工作区后执行。详见 [LooseFileLoader README](plugins/LooseFileLoader/README.md)。

### 适配记录

- [LooseFileLoader 实际运行字段核对](plugins/LooseFileLoader/analysis/Common_runtime_fields_2.0.2.0.md)
- [GameManager 偏移与签名](plugins/LooseFileLoader/analysis/GameManager_offsets_2.0.2.0.md)
- [LooseFileLoader 其余签名目标核对](plugins/LooseFileLoader/analysis/Other_pattern_targets_2.0.2.0.md)
- [UnlimitedTransmog 2.0.2.0 适配与验证](plugins/UnlimitedTransmog/analysis/Compatibility_2.0.2.0.md)

## 排查问题

- **插件没有生效**：先检查加载器是否加载了对应 DLL，再查看游戏目录 `logs/<插件名>.log` 中的版本、初始化和签名定位信息。
- **出现 `Pattern not found`**：通常需要重新核对当前游戏版本；仅命中某段字节也不等于目标函数正确。
- **资源覆盖没有生效**：检查文件名 ID、目录深度和冲突日志；新增文件后重启游戏。
- **找不到配置**：确认 INI 与插件 DLL 同目录，文件名与配置节名称匹配。
- **构建成功但部署失败**：检查复制路径、目录权限及 DLL 是否正被游戏占用。

当前公共日志写入游戏目录的 `logs` 文件夹，启动时会覆盖同名日志；排查前可先保存上一次日志。

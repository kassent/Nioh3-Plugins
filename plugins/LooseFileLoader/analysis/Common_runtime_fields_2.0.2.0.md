# LooseFileLoader 实际运行路径字段核对（Nioh 3 2.0.2.0）

## 现象与范围

按 `main.cpp -> InstallHooks() -> ModHooks.cpp` 的执行路径核对 `include/Common.h`，以当前 `Nioh3.exe.unpacked.exe`（IDA image base `0x140000000`）为准。下表针对插件实际读写的字段；`GameManager` 中仅声明但插件未调用的查询方法和 `RDBDescriptor` 的未直接访问字段不作为插件活跃字段。

## 活跃字段证据

| 字段 / ABI | 插件用途 | 2.0.2.0 证据 | 结论 |
|---|---|---|---|
| `AssetReader::archiveManager +0x08` | Deserialize mid-hook 取得 ArchiveManager | `sub_1403159A0` 构造 `v57[1]=a3` (`0x140315B4A`)，`GetArchiveInfo_1402DFB7C` 读 `[rcx+8]` (`0x1402DFB88`) | 正确 |
| `AssetReader::streamReader +0x10` | 替换读取器，GetArchiveInfo hook 检测 ModFileReader | `sub_1403159A0` 构造 `v57[2]=v20` (`0x140315B4E`)，`v57` 传给 Deserialize | 正确 |
| `AssetReader::archiveFileHandle +0x18` | 原生 GetArchiveInfo 使用；插件仅注释日志 | `sub_1403159A0` 的 `v57[3]=a11` (`0x140315B2D`)，`GetArchiveInfo_1402DFB7C` 读 `[rcx+18h]` (`0x1402DFB7F`) | 正确 |
| `AssetReader::archiveFileOffset +0x20` | 覆盖时写零 | `sub_1403159A0` 构造 `v57[4]=a10` (`0x140315B43`)，`sub_1405DE1EC` 读 `[a3+32]` (`0x1405DE349`) | 正确 |
| `AssetReader::assetFileSize +0x28` | 记录大小，覆盖时写 Mod 文件大小 | `sub_1403159A0` 构造 `v57[5]=[RDBDescriptor+0x18]` (`0x140315B61`)，`sub_1405DE1EC` 读 `[a3+40]` (`0x1405DE341`) | 正确 |
| `AssetLoadingContext::archiveManager +0x08` | 原生 Deserialize 上下文 | 构造函数 `sub_140317674` 在 `0x140317680` 写 `[rcx+8]=r8`；调用点传入 ArchiveManager `a3` | 正确 |
| `AssetLoadingContext::gameAsset +0x28` | mid-hook 取 GameAsset | `sub_140317674` 在 `0x1403176A2` 写 `[rcx+28h]=a5`，调用点 `0x140315C34` 传入 `a7` | 正确 |
| `GameAsset::typeInfoKtid +0x08` | 日志类型 ID | `sub_1403159A0` 在 `0x140315C49` 以 `[a7+8]` 查询资源 handler | 正确 |
| `ArchiveManager::assetManager +0x210`，`AssetManager::assetIdManager +0x08` | 用 GameAsset 取 fileKtid | `sub_1403159A0` 在 `0x140315CDA` 向 `GetFileKtIdFromRes_140FAE850` 传 `a3+536` (`+0x218`)；旧版对应调用同偏移 | 正确 |
| `IBaseGameAssetHandler::GetTypeName` vtable `+0x18` | 日志类型名 | `RegisterAssetHandler_143246E9C` 在 `0x143246F11` 调用 `[vtable+18h]` | 正确 |
| `IBaseGameAssetHandler::Deserialize` vtable `+0xB0` | mid-hook 处调用原生 Deserialize | `sub_1403159A0` 的 `0x140315C74`: `call qword ptr [rbx+0B0h]` | 正确 |
| `Deserialize` 第四实参 `R9` | mid-hook 将原调用的 `ctx.r9` 原样转传 | 新版 `0x140315C5E` 调用 `sub_1402F060C`，`0x140315C63` 执行 `mov r9, rax`，随后只设置 `R8/RDX/RCX` 就在 `0x140315C74` 执行虚调用；旧版 `0x1405E52F6`/`0x1405E52FB`/`0x1405E530C` 同序列 | 正确 |
| `StreamingTexContext` handler `+0x14` | `DisableStreamingLoading=1` 时清除启用标志 | 新版 vtable `0x143B1DBE0` 的 `+0x18` 返回字符串 `StreamingTexContext`，`+0x20` 函数 `0x140A86EC0` 返回 hash `0xAD57EBBA`，`+0xB0` 指向 Deserialize `0x1405DE1EC`；后者在 `0x1405DE2ED` 读取 `[handler+14h]` 控制 streaming 分支 | 正确（条件路径） |

## 发现并修正

`AssetReader::ArchiveInfo::filePath` 的起点 `+0x28` 正确，但旧声明 `char[512]` 容量错误。原生 `sub_140314898` 在 `0x140314938` 取得 `a2+40` 作路径缓冲，并在 `0x140314993` 以 **1024 字节**为 `WideCharToMultiByte` 输出容量；初始化时从 `a2+4` 清零 1060 字节 (`0x1403148E6`)，故完整结构为 `0x428` 字节。现将字段改为 `char[1024]` 并加 `sizeof` 断言。路径覆盖改为先检查容量再复制含终止符，超长路径返回与原生转换失败相同的 `-15`，避免越界及错误地保留原路径。

`AssetReader::GetArchiveInfo` 原声明返回 `bool`，而实际返回 0 或负错误码（如 `GetArchiveInfo_1402DFB7C` 返回 `0xFFFFFFF2`）。插件 hook 已使用 `int32_t`；头文件声明同步改为 `int32_t`。

## 边界与验证

当前日志的 `DisableStreamingLoading=0`，故 `assetHandler+0x14` 写入分支未执行；已按新版 `StreamingTexContext` vtable 和 Deserialize 读取点验证该条件路径。`g_gameMain` 和 `GetResIDFromRes`/`GetResHandlerFromType` 的 relocation 在本插件当前路径中仅静态初始化，未被插件直接调用；其旧 pattern 未命中不等于以上字段偏移错误。

核对方法为当前 exe 的静态反汇编/反编译与源码调用点对照；没有以实际游戏内 mod 覆盖成功代替字段证据。`RDBDescriptor::fileSize +0x18` 和 `flags +0x28` 由原生函数使用，插件本身仅透传结构指针。

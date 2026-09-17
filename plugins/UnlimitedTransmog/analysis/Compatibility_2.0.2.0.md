# UnlimitedTransmog 在 Nioh 3 2.0.2.0 的适配

## 当前状态

已修改插件源码，版本 `1.0.3 -> 1.0.4`。按用户要求保持原来的 `patchAddr1/2/3`、局部 `static`、lambda hook、NOP 和 `mov al,1` 写法；移除了上一稿额外引入的独立回调、全局 handle、初始化状态和版本锁。继续复用 `common/include/GameType.h` 中的 `ItemData` 和 `GetName()`，直接修正已核实的 ItemData 字段及两个失效/误命中的公共签名，没有保留另造的 `TransmogItemData`。

已完成全局入口签名唯一性、局部二阶段定位、调用目标、寄存器、字段和类型映射离线验证。2026-09-17 21:25 已按用户要求完成 **Release 定向构建和部署**，未进行游戏内实测。

构建命令：`cmake --build build --config Release --target UnlimitedTransmog`。输出 `build/bin/Release/UnlimitedTransmog.dll`，POST_BUILD 自动复制至 `E:/SteamLibrary/steamapps/common/Nioh3/plugins/UnlimitedTransmog.dll`。两份 DLL 大小均为 1,164,800 bytes，SHA-256 均为 `BCB59164A12FB77E26F557E22ABC747505539E75CB82B1FF9BF53B865223A77A`，部署校验一致。没有全量构建或部署其他插件。

验证样本：`Nioh3.exe.unpacked.exe`，image base `0x140000000`，SHA-256：

`326BDA907FE3D309C27F5CB7DE4262EAF6E42DDD259B66B038EEEAE3CD777721`

## 日志结论

`E:/SteamLibrary/steamapps/common/Nioh3/logs/UnlimitedTransmog.log` 在 2026-09-17 20:32:39 只写出 `GetItemData` 的 `Found updated address, new: 0x000F4DBC, old: 0x004A25F0`。同次 `NIOH3PluginLoader.log` 随后写出 `Failed to load plugin: .../UnlimitedTransmog.dll`，且没有插件自身的 `Initializing plugin` / `Plugin initialized` 行。因此本次插件未完成装载，尚未运行到 `nioh3_plugin_initialize` 和 patch 安装。

## 静态核对

以旧 `Nioh3.exe.unpacked.0225.exe` 和当前 `Nioh3.exe.unpacked.exe` 的全部可执行节扫描，并对照旧 `.0223` 与当前 2.0.2.0 的 IDA 函数逻辑：

| 项目 | 旧版 / 现状 | 2.0.2.0 证据 | 影响 |
|---|---|---|---|
| `GetItemData` | 旧 RVA `0x04A25F0`，当前日志解析为 `0x000F4DBC` | pattern 唯一命中；旧 `.0223` 的 `0x14049DF00` 与新版 `0x1400F4DBC` 均用 16-bit itemId 查表并返回 416-byte ItemData 条目 | 这个 pattern 命中正确，不是失效原因 |
| `g_resManager` | `GameType.h` 原 pattern `48 8B 05 ? ? ? ? 41 8B D7 48 8B 98`，预期 RVA `0x438B8E0` | 当前可执行节 **0 命中**；旧换装枚举函数 `sub_1423AF028` 用全局槽 `0x1443ABA40`，新版对应函数 `sub_1423430C8` 用全局槽 `0x1445B9E30` (RVA `0x45B9E30`) | 静态 relocation 已失效；此扫描紧随日志中唯一成功的 `GetItemData` 扫描，疑似装载失败阶段，需有异常栈才能确定直接异常点 |
| `ResourceManager::itemData` | 旧换装枚举函数通过全局对象 `+0x60` 取得 ItemDataManager | 新 `sub_1423430C8` 经 `qword_1445B9E30 + 0x68` 取得同一类管理器 | 即使全局槽修复，旧 `+0x60` 仍不能用于新版 |
| 三处换装 patch | `patchAddr1` / `patchAddr2` / `patchAddr3` 的原 pattern 在旧 `.0225` 各唯一匹配 | 当前 2.0.2.0 三者 **均 0 命中**；同语义的新调用点分别在 `0x142343227`、`0x142343236`、`0x142343242` | 即使装载成功，原 hook 也不会安装 |
| ItemData 的装备组字段 | 旧过滤函数 `sub_1423B3750` 分别读取 item `+0x4C/+0x50/+0x54` | 新过滤函数 `sub_142347D78` 分别读取 `+0x58/+0x5C/+0x60`；类别字段 `+0x182` 未变 | 插件当前 `GetItemDisplayType` 所读的组字段过时 |
| 换装显示类型编号 | 旧近战 `0..13`，远程 `14..16`，防具 `17..21` | 新近战 `0..15`（新增 group `4866`、`13257`），远程 `16..18`，防具 `19..23` | 旧硬编码映射错误，需同步更新 |

`g_resManager` 旧 pattern 失配与三处换装 patch 失配是两个独立问题。以上记录保留旧版失效诊断；本次修改还修复了下面进一步确认的问题。

## 本次进一步核实的字段和目标

### ItemData 与 hook 寄存器

新列表枚举函数 `0x1423430C8` 在 `0x1423431EA` 调用 `GetAt` (`0x1400F4E7C`)，`0x1423431EF` 将返回的 ItemData 指针放进 **R14**。itemId 仍取自 `+0x152`，但存放在栈 `[rsp+0x20]`，**R12 已不是 itemId**。类型参数保存在 **R13D**，在过滤调用前复制到 R8D。

因此解锁回调直接使用 R14，无需再经过 `g_resManager -> ItemDataManager -> GetItemData`。过滤 hook 恢复原来的调用前写法：把原 E8 调用 NOP 掉，在该位置的 lambda 中用 **RDX/R8** 取得原参数并调用原过滤函数；这里尚未执行原调用，参数寄存器有效。

| 本插件读取字段 | 旧偏移 | 2.0.2.0 偏移 | 证据 |
|---|---:|---:|---|
| weaponType | `0x4C` | `0x58` | `0x142347D78` 按 category=1 比较 |
| gunType | `0x50` | `0x5C` | 同函数 category=2 |
| armorType | `0x54` | `0x60` | 同函数 category=3 |
| nameHash | `0x5C` | `0x68` | `0x14034E3CC: mov eax, [r8+68h]` |
| category | `0x182` | `0x182` | 枚举和过滤函数一致 |
| sizeof(ItemData) | `0x1A0` | `0x1A0` | GetAt/GetItemData 的 416-byte 条目步长 |

名称证据链：已知 ItemData 调用 `0x14034E1C8` 生成显示名，该函数在 `0x14034E267` 调用名称 hash getter `0x14034E3B8`，随后 `mov ecx,eax` 并在 `0x14034E26E` 调用字符串查询。hash getter 的普通名称分支读取 `+0x68`；`+0x74` 是条件名称分支。插件延续旧逻辑，仅检查基础名称是否非空，不复刻带修饰的显示名。

### 旧字符串 pattern 唯一命中也不代表正确

旧 `GetLocalizedString` pattern：

```text
E8 ? ? ? ? 33 F6 48 C7 45 ? ? ? ? ? 48 8D 1D
```

在新版仍有唯一匹配，但解析目标为 **`0x141A77220`**，它处理指针/结构参数，并非按 uint32 hash 查询字符串的函数。新版正确函数为 **`0x1405A4B04`**，其包装逻辑与旧 `.0223` 的 `0x1402D539C` 对应：将 ECX hash 放到 R8D，从语言管理器读取 `+0x1CE4` 的语言编号，再跳转到按语言/hash 查表的实现 `0x14063D9C4`。

在 `GameType.h` 中更新 `GetLocalizedString` 的 call-site pattern，保留原来的 `REL::Relocation` 及 `ItemData::GetName()` 调用方式。同时更新 `g_resManager` 静态 relocation 的签名为 `4C 8B 05 ? ? ? ? 44 8B FF`，全局唯一匹配 `0x1423431C7`，RIP-relative 解出全局槽 `0x1445B9E30`。

`ResourceManager::itemData` 的 `+0x68` 已由原生指令确认，但此次没有扩展修改其余 ResourceManager 布局；插件解锁路径直接用 R14，因此不访问该管理器字段。公共头文件其他未用于本插件的成员仍需单独适配，不能把本次验证理解为整个 GameType.h 已适配完成。

## 新签名与 hook 地址

下表全部是 **RVA**。入口、第三处检查和两个公共签名在全部可执行节均 **1 命中**。第二处沿用原来的局部二阶段查找：其短前缀全局有 **6 个匹配**，但在已唯一定位的 patchAddr1 后 `0x100` 范围内仅 **1 个**，加 6 指向过滤调用。函数目标按 `call + 5 + int32(call+1)` 解析。

| 用途 | Pattern | 调用点 RVA | 被调用函数 RVA | hook / patch RVA |
|---|---|---:|---:|---:|
| 已解锁检查 | `E8 ? ? ? ? 84 C0 74 ? 45 8B C5 49` | `0x02343227` | `0x0054F124` | `0x0234322C` |
| 装备类型过滤 | 局部 `45 8B C5 49 8B D6`，match + 6 | `0x02343236` | `0x02347D78` | `0x02343236` |
| 武士/忍者限制检查 | `E8 ? ? ? ? 84 C0 74 ? 44 0F B7 44 24 20` | `0x02343242` | `0x02347DC4` | `0x02343242`，写入 `B0 01 90 90 90` |
| 本地化字符串 | `E8 ? ? ? ? 48 8B C8 4C 8B C3 66` | `0x0034E26E` | `0x005A4B04` | 不安装 hook |

保留原来的两个 mid-hook 和一处字节补丁：

- 解锁：有效装备组且基础名称非空时返回 true。
- 类型共享：在原调用位置进入 lambda，调用原过滤函数；失败且为近战类型 `0..3` 时，保持原有四个 if 分支重试 `0↔1 / 2↔3`。
- 武士/忍者限制：开启共享时用 `mov al,1; nop; nop; nop` 替换原 E8 调用。

原过滤函数的第一个参数未使用，仍传 nullptr。过滤 hook 的 RDX/R8 是调用前参数；解锁 hook 仍在调用后，通过 R14 取得 ItemData。

## 修改范围

- 两个配置开关、局部 static lambda、DllMain/BranchTrampoline 保持原有组织方式；没有新增版本锁和初始化状态机。
- patchAddr1 唯一入口 + patchAddr2/3 局部查找保留；把 patchAddr3 的定位提前到任何字节写入之前，签名查找失败返回 false。
- 继续使用公共 GameType.h，修正武器组、名称 hash 偏移和 `GetLocalizedString` / `g_resManager` 签名；不另造本插件专用结构体。
- 解锁直接读取 R14 中的 ItemData，保留原来的 GetName() 和 GetItemDisplayType() 检查。
- 类型映射同步为近战 `0..15`、远程 `16..18`、防具 `19..23`；新增近战 group `4866/13257`。

## 可重复离线验证

```powershell
python plugins/UnlimitedTransmog/analysis/verify_2_0_2.py 'E:/SteamLibrary/steamapps/common/Nioh3/Nioh3.exe.unpacked.exe'
```

脚本只读 EXE 和当前 main.cpp，依赖 pefile/capstone，不构建、不写游戏文件，也不执行 PE。验证内容：

1. 四条全局签名全可执行节唯一匹配（计入重叠匹配），调用和 RIP-relative 目标正确；第二处短前缀在唯一入口锚定范围中唯一。
2. 解锁的 R14、过滤调用前的 RDX/R8、itemId/category 读取指令和补丁范围不重叠。
3. 从公共头文件重建 packed ItemData 布局，确认读取字段偏移与原生指令一致。
4. 对三个纯寄存器原生映射函数进行有限指令解释，核对源码 24 个映射及越界输入。
5. 源码继续复用 GameType.h / ItemData::GetName()，解锁回调不再经 g_resManager 查找或读取 ctx.r12。

离线脚本全部通过，`git diff --check` 通过；后续 Release 编译链接及部署也已成功。游戏内效果尚未实测，仍需启动游戏检查插件初始化日志，并实测两个开关各自和共同启用时的换装列表。

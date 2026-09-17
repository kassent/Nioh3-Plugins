# GameManager 相关偏移：2.0.2.0 对照

## 现象

`g_gameMain` 的旧 pattern 未命中。全局指针槽从 RVA `0x4566990` 移到 `0x4759910`（VA `0x144759910`）；需要区分全局地址变化与对象内部布局变化。

## 新旧样本证据

| 链路 | 旧版指令 / 函数 | 2.0.2.0 指令 / 函数 | 结论 |
|---|---|---|---|
| `g_gameMain -> archiveManager` | `sub_14068E80C` 的 `0x14068E827`: `mov rcx, [rcx+530h]`，随后调用 `GetTypeHandlerFromType_140183E5C` | `sub_14039B0E8` 的 `0x14039B103`: `mov rcx, [rcx+530h]`，随后调用 `sub_1402386FC` | `GameManager::archiveManager` 仍为 `+0x530`，且仍是指针 |
| `ArchiveManager -> AssetManager` | `sub_14025B788` 用 `a3+528` (`+0x210`) 作为资源管理子对象 | 对应的 `sub_14019F320` 仍用 `a3+528` | `ArchiveManager::assetManager` 仍从 `+0x210` 开始 |
| `ArchiveManager -> AssetIdManager` | `sub_14025B788` 的 `0x14025B84E` 将 `a3+536` (`+0x218`) 传给资源 ID 查询 | `sub_14019F320` 的 `0x14019F3E6` 同样传 `a3+536` 给 `sub_1404A7A30` | `AssetManager::assetIdManager` 仍为 `+0x08` (`0x210+0x08=0x218`) |
| 相关状态字段 | `sub_14025B788` 使用 `a3+616` (`+0x268`) 和 `a3+728` (`+0x2D8`) | 对应的 `sub_14019F320` 使用相同偏移 | 这两个 archive-relative 字段未移动；`+0x2D8 = AssetManager+0xC8` |

`GetResFileIdByFileKtid_140773EAC` / `sub_140609F74`、`GetResItemById_1409CF148` / `sub_1407AFE94`、`GetResFileIdFromRes_140591960` / `sub_1404A7A30` 的内部字段访问偏移也一致。旧版 `GetTypeHandlerFromType_140183E5C` 与新版 `sub_1402386FC` 均访问 ArchiveManager 的 `+0x180` 至 `+0x1A8` 查找表字段。

## Pattern 更新验证

以下三个新 pattern 在旧版与 2.0.2.0 的全部可执行节中分别只匹配一次；按现有 `REL::Pattern` 位移参数解码，均指向预期的函数或全局指针槽。已同步更新 `plugins/LooseFileLoader/include/Common.h` 与 `common/include/GameType.h` 的 pattern 和预期 RVA。

| 符号 | 新 pattern | 旧版匹配点 → 目标 VA | 2.0.2.0 匹配点 → 目标 VA | 位移参数 |
|---|---|---|---|---|
| `GetResIDFromRes` | `E8 ? ? ? ? 8B C8 44 8B C6` | `0x14025B84E` → `0x140591960` | `0x14019F3E6` → `0x1404A7A30` | `0, 1, 5` |
| `GetResHandlerFromType` | `E8 ? ? ? ? 45 33 C9 4C 89 4D` | `0x14013FEDF` → `0x140183E5C` | `0x14004532B` → `0x1402386FC` | `0, 1, 5` |
| `g_gameMain` | `48 8B 15 ? ? ? ? 0F 57 D2` | `0x14004FA75` → `0x144566990` | `0x1400DF2F1` → `0x144759910` | `0, 3, 7` |

## 结论

已核对的 `g_gameMain -> archiveManager -> assetManager -> assetIdManager` 链路没有偏移变化；变化的是 `g_gameMain` 全局槽地址及相关函数地址。`g_gameMain` 本身是 `GameManager*` 的存储槽，不是对象地址。头文件中其余尚未逐字段对照的 `AssetManager` 成员不在本次结论范围内。

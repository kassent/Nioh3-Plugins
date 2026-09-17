# LooseFileLoader 其余 pattern 目标核对（Nioh 3 2.0.2.0）

对 `Nioh3.exe.unpacked.0223.exe`（旧版）和 `Nioh3.exe.unpacked.exe`（2.0.2.0）全体可执行节扫描。以下每条 pattern 在**每个版本中均只匹配一次**；按源码中的 `dstOffset/dataOffset/instructionLength` 解码，并对照两版目标函数的反汇编/反编译逻辑。地址均为 RVA。

| Pattern 对应目标 | 旧版目标 | 2.0.2.0 目标 | 目标逻辑对照 |
|---|---:|---:|---|
| `GetResIdByFileKtid` | `0x0773EAC` | `0x0609F74` | 两版函数均为 `0x10F` 字节；按 fileKtid 查询索引，找不到返回 `0xFFFFFFFF`；分支及字段访问相同 |
| `GetResItemById` | `0x09CF148` | `0x07AFE94` | 两版函数均为 `0x27` 字节；检查 ID 范围后从资源项数组计算并返回条目地址 |
| `GetFileKtIdFromRes` | `0x13F5A80` | `0x0FAE850` | 两版函数均为 `0x123` 字节；先由资源对象取得资源 ID，再反查 fileKtid，失败返回 `0xFFFFFFFF` |
| `GetArchiveInfo` | `0x05E8C50` | `0x02DFB7C` | 两版函数均为 `0x22` 字节；读取 `AssetReader+0x18` 的句柄，经 `archiveManager+0x08 -> +0xD8` 转发，空句柄返回 `-14` |

对应扫描调用点：`GetResIdByFileKtid` 旧/新为 `0x0214A4B`/`0x0366E3F`；`GetResItemById` 为 `0x01C9C78`/`0x0207A38`；`GetFileKtIdFromRes` 为 `0x0214FBD`/`0x0367725`；`GetArchiveInfo` 为 `0x02AEE00`/`0x05DE334`。前三项用 `0,1,5` 解码 `E8 rel32`；第四项同样用 `0,1,5`。这些点都分别指向表中的目标，而非仅命中相似字节。

## 实际 hook pattern

| Hook | 旧版匹配点 → 目标 | 2.0.2.0 匹配点 → 目标 | 语义对照 |
|---|---|---|---|
| `GetArchiveInfo` | `0x02AEE00` → `0x05E8C50` | `0x05DE334` → `0x02DFB7C` | 同上，hook 与成员函数声明共用同一 pattern |
| `RegisterAssetHandler` | `0x0D8DFD1` → `0x0AC53E8` | `0x26F5E69` → `0x3246E9C` | 两版均检查注册状态、handler vtable，向类型表插入 hash 与 handler 指针，并通过 `AL` 返回成功标志；新函数 IDA 的 `void` 推断与实际 `mov al, bl`、调用方 `test al, al` 不符 |
| `DeserializeAsset` 调用点 | `0x05E530C` | `0x0315C74` | 两版均在 `MountResource` 对应函数内以 `call [rbx+0xB0]` 调用 handler 的 Deserialize，入参 `RCX/RDX/R8` 及后续结果保存顺序相同 |

以上 hook pattern 在两个版本的全体可执行节中也各只匹配一次。`GetFileKtIdFromRes` 与 Deserialize 调用点位于 LooseFileLoader 当前资源覆盖路径；其余两个资源查询函数目前仅静态 relocation 初始化，`GetArchiveInfo` 和 `RegisterAssetHandler` 为运行时 hook。此处证明静态目标一致，不等于已经完成新版本游戏内覆盖加载测试。

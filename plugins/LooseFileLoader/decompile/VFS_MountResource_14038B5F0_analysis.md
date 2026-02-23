# Nioh3 `VFS_MountResource_14038B5F0` 资源加载流程分析

## 1. 结论先行
- `0x14038B5F0` 是“资源实例化/挂载”核心函数，不是直接磁盘读取函数。
- `.fdata` 的实际读取发生在它的调用者（如 `sub_14062B724` / `sub_1430D759C`）里，读取完成后把内存流对象传给 `VFS_MountResource_14038B5F0`。
- `RDB/RDX` 的关系与第三方 C# 代码一致：`RDB` 给出 `FileKtid/Type/Offset/FDataId`，`RDX` 把 `FDataId -> 0xXXXXXXXX.fdata` 映射出来。

## 2. 与你提供的 C# 读取器对应关系
来自 `RDBReader.cs` / `RDXReader.cs`：
- `RDBEntry.FileKtid`：资源哈希（运行时错误日志里会通过 `Rdb_FindResHashByKtid_1413F13F0` 取回）。
- `RDBEntry.EntryType`：资源类型 ID（运行时 `a7+8`，用于选择类型处理器 `sub_14038A594`）。
- `EntryLocation.Offset/SizeInContainer/FDataId`：定位到 `.fdata` 里资源数据位置。
- `RDXEntry(Index, FileId)`：`FDataId -> 0x{FileId}.fdata`。

## 3. 样本文件快速验证
对 `package/root.rdb`、`package/root.rdx` 的前几项解析结果（与 C# 结构一致）：
- `root.rdb` 头：`_DRK`，`folder=data/`
- 示例条目：
  - `fileKtid=0x00007831`
  - `type=0x00000008`
  - `offset=0x154d2c680`（64-bit 偏移，说明存在 0x11 长 metadata）
  - `fdataId=23 -> container=0x281dfb64.fdata`
- `0xf691b8e2.fdata` 头部存在 `PDRK`/`IDRK`，第一块中能看到：
  - `typeInfoKtid=0xBBD39F2D`（你 `demo.txt` 里映射为 `AssetData`）
  - 后续数据里出现 `78 9C`（zlib 特征）

## 4. 关键调用链
- `sub_14062B724`:
  1. 通过 `sub_1405881A0(v2+536, entryPtr)` 计算条目线性索引。
  2. 构造/打开数据流对象（由路径或索引定位到底层容器）。
  3. `sub_14026FBA0` 把指定长度数据读入内存缓冲。
  4. 调用 `VFS_MountResource_14038B5F0(...)` 完成实例化。
- `sub_1430D759C` 也是类似流程：先读，再调用 `VFS_MountResource_14038B5F0`。

因此：`0x14038B5F0` 的输入已经是“可读流/已装载块”，它负责构建资源对象与挂载回调。

## 5. `VFS_MountResource_14038B5F0` 详细流程
按指令流（`0x14038B5F0`）归纳：

1. **初始化输入流包装**
- 从 `a8+40` 取压缩模式：`compression = ((flags >> 20) & 0x3F)`。
- `compression == 0` 时调用 `sub_140C6F5D8` 组装默认流包装；否则直接用传入流对象。
- 若 `a6 != 0`，额外调用 `sub_1430CEAE8` 再包一层（记录上下文和状态）。

2. **可选解码器链（`a12`）**
- 当 `a12` 非空且流程分支允许时，根据 `compression` 尝试初始化特定解码器：
  - 模式 1：`sub_1410D0EE0(...)`
  - 模式 3：`a12[2]` 的虚函数（`[vft+0x30]`）
  - 模式 4：`sub_1430D8D0C(...)`
- 若解码初始化失败：释放输入流，清标志位，返回失败。

3. **构建创建参数**
- 需要的描述内存大小：`24 + 32 * *(a8+48)`。
- 优先栈上临时分配；超过阈值则走 `a3+32` 的分配器。
- 清空并快照 `a7` 的部分状态字段（把旧状态转存到工作区）。
- `sub_14038C388`：拷贝 2 个 dword 范围信息。
- `sub_14038C344`：打包创建上下文（`a4/a3/range/a7/a13`）。
- `sub_14038A594(a3, *(a7+8))`：按类型 ID 找资源处理器。
- `sub_14038C1A4(...)`：构建“子块/分片描述表”供资源工厂消费。

4. **真正实例化资源对象**
- 调用处理器虚函数 `handler->vft[0xB0]` 创建资源实例。
- 结果写回 `*(a7+16)`。
- 成功则可能触发后续回调（`sub_140920B60` 或对象虚函数回调）。

5. **失败处理**
- `sub_14026F4A4(a7, ...)` 取资源名称（可能为空）。
- `Rdb_FindResHashByKtid_1413F13F0(a3+0x218, a7)` 取资源 hash。
- 打日志：`can't create resource Name[%s] Hash[0x%08x]`。

6. **清理**
- 释放临时对象/包装器：`sub_14038B050`, `sub_14038B10C`, `sub_14038C41C`, `sub_14038C3F4`。
- 若临时描述内存来自堆分配则释放。

## 6. 关键子函数语义（按重要度）
- `sub_14038A594(ctx, key)`：
  - 多级索引查找（树/分页结构），返回类型处理器指针。
  - 在本函数中 `key = *(a7+8)`，即资源类型 ID。

- `sub_14038C1A4(node, ctx, outBuf, ?, optionalMeta)`：
  - 构建每个子块的描述（条目大小 0x20）。
  - 每条目使用 `node+56` 的 12 字节记录（3 个 dword）并计算数据推进步长。
  - 最终设置输出头：`count / entries / reserved`。

- `Rdb_FindResHashByKtid_1413F13F0(indexRoot, entryPtr)`：
  - 先调用 `sub_1405881A0(indexRoot, entryPtr)` 计算条目线性索引。
  - 再从索引结构取对应 hash（失败返回 `0xFFFFFFFF`）。
  - 这是你 hook 里 `Rdb_FindIndexByKtid(a3 + 536, a7)` 的同类逻辑。

- `sub_14038C344`：
  - 封装资源创建上下文（owner、archive、range、entry、callback context）。

- `sub_14038C388`：
  - 简单复制 2 个 dword 区域参数。

- `sub_14026FBA0(streamState, stream, dst, size)`：
  - 执行流读取，要求返回长度必须等于 `size`；成功后记录 `dst`。
  - 说明读取动作在 mount 前完成。

## 7. 对 `fdata` 加载机制的整体理解
1. 通过 RDB 条目拿到逻辑资源（`FileKtid`, `EntryType`, `Offset/FDataId`）。
2. 通过 RDX 把 `FDataId` 转成具体 `0xXXXXXXXX.fdata`。
3. 上层函数先把目标数据块从容器流中读出（含可能压缩块）。
4. `VFS_MountResource_14038B5F0` 根据类型和压缩模式选择处理器/解码器。
5. 组装分片描述并调用类型工厂，最终生成运行时资源对象。

## 8. 逆向层面的注意点
- 当前能稳定确认的语义：查找/挂载/实例化主路径。
- `sub_14038C1A4` 的位标志细节和某些字段命名仍属于“高可信推断”，但不影响主流程判断。
- 如果你下一步要做“Loose File 覆盖加载”，最稳妥切点仍是：
  - 读取阶段（替换 `sub_14026FBA0` 前的数据源），或
  - `VFS_MountResource_14038B5F0` 入参中的流对象/条目对象。

## 9. `fdata` 读取阶段深挖（新增）

这一节只看“从索引定位到 `.fdata` 并把字节读出来”的链路，不包含最终资源实例化。

### 9.1 关键读取链路（运行时）
核心调用链：
- `sub_14062B724`（读取任务执行）
  - `sub_1405881A0(a3+0x218, entryPtr)`：把条目指针映射为线性索引。
  - `resolver->vft[0x80](index, outPath, outOffset, outMeta)`：得到容器路径和偏移信息（对应 `sub_14062C4F8` 语义）。
  - `sub_14062C304`：按路径打开容器文件，构造读流对象（`off_1436E3D70`）。
  - `sub_1400D529C(handle, offset+0x38, SEEK_SET-ish)`：seek 到数据区。
  - `sub_14026FBA0(desc, stream, dst, size)`：按长度精确读取（内部走流虚函数 `+0x20`，即 `sub_14062EC48`）。
  - 读取完后才调用 `VFS_MountResource_14038B5F0`。

结论：`.fdata` 读取明确发生在 `VFS_MountResource_14038B5F0` 之前。

### 9.2 路径生成逻辑（`sub_14062C4F8` + `sub_14062C918`）
`sub_14062C4F8` 会根据条目标志走两类路径：

1. 外部存储（`.file`）  
- 生成：`<base>/<hash_low8>/0x%08x.file`
- 与你 C# 的 `StorageExternal` 逻辑一致。

2. 内部存储（`.fdata`）  
- 调 `sub_14062C918` 生成：
  - `%s/0x%08x.fdata`
  - 或 `%s/%s/0x%08x.fdata`（有额外目录层时）
- 其中容器 hash 与目录层来自预加载索引表（可视为 RDX/分组表的运行时展开）。
- 同时返回条目偏移（block 起始偏移）和相关元信息。

### 9.3 为什么代码固定 `+0x38` / `-0x38`
在 `sub_14062B724` 里可见：
- `seek_offset = entry_offset + 0x38`
- `read_size = entry_span - 0x38`

这与 `.fdata` 实测完全一致：
- `IDRK` entry 头部固定 0x30（48）字节；
- 另外还有 0x08 运行时前置字段；
- 合计跳过 `0x38` 后才是 payload。

样本 `0xf691b8e2.fdata` 第一条：
- `entrySize = 0x45565`
- `dataSize  = 0x4552D`
- `entrySize - dataSize = 0x38`（与代码吻合）

### 9.4 读流对象的行为（`off_1436E3D70`）
`sub_14062C304` 构造的对象关键字段：
- `a1[1] = archiveCtx`
- `a1[2] = fileHandle`
- `a1[3] = currentPos`

读取调用 `sub_14062EC48`：
- 最终进入底层 `sub_1400D2B54` 提交 I/O；
- 成功后累加 `a1[3]`（流位置）；
- 失败返回 0（`sub_14026FBA0` 以“读取长度必须等于请求长度”判定成功）。

### 9.5 压缩与解码发生在哪一层
- `fdata` 读取阶段仅负责“按偏移拿到 payload 字节”；
- zlib/其他压缩解码与类型装配在后续挂载路径里完成（`VFS_MountResource_14038B5F0` 及其分支）；
- 这也解释了为什么你在 `fdata` 中能直接看到 `78 9C` 特征。

### 9.6 对实现 LooseFile 覆盖的直接启示
如果你要替换 `.fdata` 读取结果，最稳的切点是：
- `sub_14062C4F8` / `sub_14062C918`：改路径解析（把内部容器改为外部路径）；
- `sub_14062C304`：改打开逻辑（优先 loose 文件）；
- `sub_14026FBA0`：改最终读入字节（最直接，但要保证长度/对齐与后续流程兼容）。

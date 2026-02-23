# Nioh3 `rdx` / `rdb` / `fdata` 关系与结构说明

本文基于以下代码与样本：
- `plugins/LooseFileLoader/RDBExplorer/RDBExplorer/Core/RDXReader.cs`
- `plugins/LooseFileLoader/RDBExplorer/RDBExplorer/Core/RDBReader.cs`
- `plugins/LooseFileLoader/RDBExplorer/RDBExplorer/Core/ArchiveExploler.cs`
- `plugins/LooseFileLoader/package/root.rdx`
- `plugins/LooseFileLoader/package/root.rdb`
- `plugins/LooseFileLoader/package/0xf691b8e2.fdata`

## 1. 三者关系（先看全局）

`rdb` 决定“资源索引与定位参数”，`rdx` 决定“fdata 容器编号映射”，`fdata/.file` 存放实际资源块。

读取链路：
1. 在 `rdb` 里按资源条目拿到 `Offset/SizeInContainer/FDataId/NewFlags/FileKtid`。
2. 若 `NewFlags == Internal (0x401)`，通过 `rdx` 的 `Index -> FileId` 得到 `0xXXXXXXXX.fdata` 文件名。
3. 若 `NewFlags == External (0xC01)`，直接构造 `data/<hash_low8>/0xXXXXXXXX.file`。
4. 打开容器，`Seek(Offset)` 到条目起点。
5. 读取条目头 `IDRK`（KRDIHeader），再按压缩类型读取/解压 payload。

## 2. `rdx` 结构定义

`RDXReader` 每 8 字节读一条：

```csharp
struct RdxEntry {
    ushort Index;   // FDataId
    ushort Marker;  // 常见 0xFFFF
    uint   FileId;  // 对应容器名 0x{FileId:x8}.fdata
}
```

样本 `root.rdx`（前 12 条）：
- `index=0 -> fileId=0x02816AA5`
- `index=1 -> fileId=0x03E41543`
- `index=2 -> fileId=0x03FA9E7B`
- ...

## 3. `rdb` 结构定义

### 3.1 文件头（`_DRK`）

`RDBReader` 读取的头：

```csharp
struct RDBHeader {
    char Magic[4];      // "_DRK"
    int  Version;
    int  HeaderSize;
    int  SystemId;
    int  FileCount;
    uint DatabaseId;
    char FolderPath[8]; // 例如 "data/"
}
```

样本 `root.rdb` 头部：
- `Magic=_DRK`
- `Version=0x30303030`（按 int 显示为 `808464432`，字节上是 ASCII "0000"）
- `HeaderSize=32`
- `SystemId=14`
- `FileCount=219378`
- `DatabaseId=0xADD53B4B`
- `FolderPath=data/`

### 3.2 条目头（`IDRK`，48 字节）

```csharp
struct RDBEntryHeader {
    char  Magic[4];      // "IDRK"
    uint  Version;
    long  EntrySize;
    long  DataSize;      // C# 中 metadataSize
    long  FileSize;      // 原始（解压后）资源大小
    uint  EntryType;
    uint  FileKtid;
    uint  TypeInfoKtid;
    uint  Flags;         // 压缩类型等位域
}
```

`RDBReader` 的关键计算：

```text
paramsSize = EntrySize - DataSize - 48
```

这表示：
- 48 字节是 `IDRK` 条目头。
- `paramsSize` 是头后参数区（`UnkContent`）。
- 尾部 `DataSize` 字节是位置描述区（`ParseLocation` 读取）。

### 3.3 位置描述区（`DataSize`）

仅处理两种格式：

1) `DataSize == 0x11`（17 字节，64 位偏移）
```c
struct Location17 {
    ushort NewFlags;      // 0x401 internal / 0xC01 external
    byte   OffHi8;
    byte   Pad3[3];
    uint   OffLow32;
    uint   SizeInContainer;
    ushort FDataId;
    byte   Pad1;
};
Offset = (OffHi8 << 32) | OffLow32;
```

2) `DataSize == 0x0D`（13 字节，32 位偏移）
```c
struct Location13 {
    ushort NewFlags;
    uint   Offset32;
    uint   SizeInContainer;
    ushort FDataId;
    byte   Pad1;
};
```

样本统计（`root.rdb`）：
- `DataSize=0x0D`: `186237`
- `DataSize=0x11`: `33141`
- `NewFlags=0x401`: `219369`
- `NewFlags=0xC01`: `9`

## 4. `fdata` / `.file` 条目结构（KRDI）

### 4.1 容器与条目

`ArchiveExploler.GetEntryData` 在 `Offset` 处读 `IDRK` 块：
- `ReadKRDIContainer()` 校验 magic 为 `IDRK`
- 读取 56 字节 `KRDIHeader`
- 读取 `ParamCount * 12` 的参数表
- 读取 `ParamDataSize` 的参数数据
- 后续读取 payload（压缩或原始）

样本 `0xf691b8e2.fdata`：
- 文件起始 `0x00`: `PDRK0000`（容器前缀）
- `0x10` 开始是 `IDRK0000` 条目头
- 首条 `KRDIHeader`：
  - `AllBlockSize=0x45565`
  - `CompressedSize=0x4552D`
  - `UncompressedSize=0x80B60`
  - `Flags=0x00100000`（压缩类型=1，即 zlib）
  - `ParamCount=0`

> 说明：该样本中 `0x10 + AllBlockSize == 文件总长度`，即 `PDRK(16字节) + 1个IDRK块`。

### 4.2 KRDI 数据结构

```csharp
struct KRDIHeader {       // 56 bytes
    char   Magic[4];       // "IDRK"
    char   Version[4];     // "0000"
    long   AllBlockSize;   // 头+参数表+ParamData+payload
    long   CompressedSize; // payload压缩后长度
    long   UncompressedSize;
    int    ParamDataSize;
    int    HashName;
    int    HashType;
    uint   Flags;          // 压缩类型位于 [25:20]
    uint   ResourceId;
    int    ParamCount;
}

struct KRDIParam {        // 12 bytes
    int  Type;
    uint Unk;
    int  HashName;
}
```

压缩类型取法：

```text
compressionType = (Flags >> 20) & 0x3F
```

代码路径中的类型语义：
- `1`: Zlib
- `4`: Extended（分块 zlib，块头 `ushort zSize + 8字节扩展头`）
- `3`: Encrypted（代码里当前按原样读 `UncompressedSize`）
- 其他：按未压缩读

## 5. 提取流程（Extract）

对应 `ArchiveExploler.GetEntryData()`：

1. 由 `entry.Location.ContainerPath` 拼接容器路径。
2. 打开容器文件。
3. 若 `Internal`，`Position = entry.Location.Offset`；外置 `.file` 默认从 0 开始。
4. `ReadKRDIContainer` 读取 KRDI 头与参数区。
5. 根据 `compressionType`：
   - `Zlib / Extended`: 分块解压到目标缓冲（每块期望上限 0x4000）。
   - `Encrypted`: 当前实现直接读 `UncompressedSize`。
   - `Raw`: 直接读 `UncompressedSize`。
6. 返回字节数组并写盘。

## 6. 回写流程（Inject）

对应 `ArchiveExploler.InjectData()`：

1. 先从旧位置读取原始 KRDI（保留头字段/参数表/ParamData）。
2. `CreateModifiedIDRK` 构造新块：
   - 清除压缩位：`noCompressionFlags = rawFlags & ~(0x3F << 20)`
   - `AllBlockSize = 56 + ParamCount*12 + ParamDataSize + modData.Length`
   - `CompressedSize = UncompressedSize = modData.Length`
   - 参数区保持原值，payload 换成 `modData`
3. 追加写入容器尾部（16 字节对齐后 append）。
4. 更新内存里的条目定位：
   - `Offset = newOffset`
   - `SizeInContainer = fullModdedBlock.Length`
   - `FileSize = modData.Length`
5. `UpdateRDBFile` 回写 `root.rdb`：
   - `EntryOffset + 24` 写新 `FileSize`
   - 定位到 metadata 区（`EntryOffset + 48 + UnkContent.Length`）
   - 写 `NewFlags`
   - 写新偏移（`0x11` 写高8+低32；`0x0D` 写32位）
   - 写新 `SizeInContainer`

## 7. 关系总结（面向实现）

- `rdb` 是资源目录与定位参数。
- `rdx` 是 `FDataId -> fdata文件名` 映射。
- `fdata/.file` 才是实体数据块，块内结构为 `KRDI(IDRK)`。
- 实际提取/回写不需要重建整个容器索引，核心是：
  - `rdb` 定位到块
  - 解析 KRDI
  - 改块后 append
  - 回写 `rdb` 的偏移与块长

## 8. 实操注意点

1. `DataSize=0x0D` 的偏移是 32 位，回写到超 4GB 容器偏移会溢出。
2. 当前注入策略是“追加新块 + 改索引”，旧块不删除，会增大容器。
3. `Encrypted(3)` 当前逻辑未解密，仅原样读取，可能不适用于所有条目。
4. `root.rdb` 的 `Version` 实际字节常见为 ASCII `"0000"`，按 `int` 显示会是十进制大数。

# StreamFileReader vtable 分析（`fileReaderVtbl_143709F20`）

## 1) vtable 布局

`fileReaderVtbl_143709F20`（`.rdata:0x143709F20`）共 6 个槽：

- `+0x00`: `sub_1430EC494`
- `+0x08`: `sub_1405E6E6C`
- `+0x10`: `sub_1405E9C74`
- `+0x18`: `sub_1430ED9C0`
- `+0x20`: `ReadBytes_1405EABD8`
- `+0x28`: `sub_142E74714`

它的基类/默认表是 `off_143709EF0`，其中：

- `+0x10 = sub_142E7477C`（按 byte 循环调用 `+0x18`）
- `+0x18 = _purecall`（要求子类实现单字节读取）
- `+0x20 = sub_142E74718`（按 byte 循环读 buffer）
- `+0x28 = sub_142E74714`（默认返回 0）

这能说明该接口本质是一个“字节流读取器”抽象。

## 2) 各虚函数语义推导

### [0] `sub_1430EC494` (`+0x00`)

- 行为：调用 `sub_1405E6748(a1)` 做清理，然后在 `(a2 & 5) == 5` 时释放 `a1`（48 字节对象）。
- 结论：析构/删除析构（scalar deleting destructor）语义。

### [1] `sub_1405E6E6C` (`+0x08`)

- 行为：若 `this+0x10` 句柄存在，则调用 `sub_14037CA60(ctx+216, &handle, this+0x28)` 关闭/解绑句柄，并清零 `handle / ctx / pos`。
- 结论：`Close()` / `Reset()`。

### [2] `sub_1405E9C74` (`+0x10`)

- 行为：基于底层句柄当前位置做相对移动，目标位置被 clamp 到 `[0, fileSize]`；成功后 `this+0x18 += moved`。
- 结论：`Skip/SeekRelative(delta)`，返回实际移动字节数。

### [3] `sub_1430ED9C0` (`+0x18`)

- 行为：调用 `sub_14037F0F8(..., a2, 1, &v7, 0)`，即请求读取 1 字节到 `a2`；成功时累计 `this+0x18 += v7`。
- 结论：`ReadByte(void* outByte)`（返回 0/1）。

### [4] `ReadBytes_1405EABD8` (`+0x20`)

- 行为：最终走 `sub_14037D054(handle, -1, dst+offset, size, &done, ...)`，读 `size` 字节到 `dst+offset`，并累计 `this+0x18 += done`。
- 结论：批量读取 `Read(void* dst, size_t dstOffset, size_t size)`。

### [5] `sub_142E74714` (`+0x28`)

- 行为：直接返回 0。
- 结论：保留槽/可选能力槽（当前实现 no-op）。

## 3) 对应 C++ 虚表定义（逆向建模）

```cpp
// guessed from fileReaderVtbl_143709F20
struct StreamFileReader;

using FnDtor      = StreamFileReader* (__fastcall*)(StreamFileReader* self, unsigned char deleteFlags);
using FnClose     = long long         (__fastcall*)(StreamFileReader* self, long long a2, long long a3);
using FnSkipRel   = unsigned long long(__fastcall*)(StreamFileReader* self, long long delta);
using FnReadByte  = long long         (__fastcall*)(StreamFileReader* self, void* outByte);
using FnReadBytes = unsigned long long(__fastcall*)(StreamFileReader* self, void* dst, unsigned long long dstOffset, unsigned long long size);
using FnReserved  = long long         (__fastcall*)(StreamFileReader* self);

struct StreamFileReaderVTable {
    FnDtor      dtor;       // +0x00 -> 0x1430EC494
    FnClose     close;      // +0x08 -> 0x1405E6E6C
    FnSkipRel   skipRel;    // +0x10 -> 0x1405E9C74
    FnReadByte  readByte;   // +0x18 -> 0x1430ED9C0
    FnReadBytes readBytes;  // +0x20 -> 0x1405EABD8
    FnReserved  reserved0;  // +0x28 -> 0x142E74714 (always 0)
};

inline constexpr StreamFileReaderVTable kFileReaderVtbl_143709F20 = {
    reinterpret_cast<FnDtor>(0x1430EC494),
    reinterpret_cast<FnClose>(0x1405E6E6C),
    reinterpret_cast<FnSkipRel>(0x1405E9C74),
    reinterpret_cast<FnReadByte>(0x1430ED9C0),
    reinterpret_cast<FnReadBytes>(0x1405EABD8),
    reinterpret_cast<FnReserved>(0x142E74714),
};
```

## 4) 对象布局（辅助）

从 `sub_1430EC494` 的释放大小和 `sub_1405E6E6C/sub_1405E7A48` 的字段访问可得该对象是 `0x30` 字节：

```cpp
struct StreamFileReader {
    const StreamFileReaderVTable* vtbl; // +0x00
    void* ownerCtx;                     // +0x08
    void* fileHandle;                   // +0x10
    unsigned long long logicalPos;      // +0x18
    unsigned long long unk20;           // +0x20
    unsigned char closeMode;            // +0x28 (used by close)
    unsigned char pad29[7];
};
static_assert(sizeof(StreamFileReader) == 0x30);
```

`unk20`/`reserved0` 的最终业务语义在当前样本中仍未直接观察到写入路径，建议后续结合调用点继续命名。

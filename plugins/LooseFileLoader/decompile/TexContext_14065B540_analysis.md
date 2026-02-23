# TexContext 解析函数 0x14065B540 分析

## 1. 入口定位

- 目标函数：`0x14065B540`（当前为 `sub_14065B540`）
- 在资源挂载主流程中，它作为类型处理器（TypeHandler）的虚函数被调用（对应你前面定位的 `call qword ptr [rbx+0B0h]` 分发链）。
- 该函数本身不是“直接读完纹理数据”的重函数，而是 TexContext 路径的入口与调度器。

调用链主干：

1. `0x14065B540`  
2. `0x14065B67C`  
3. `0x14065B6F8`  
4. `0x14065B7D8`  
5. `0x14065D864`  
6. `0x14065BB40`（纹理主读取/组装重函数）

## 2. 0x14065B540 做了什么

核心动作：

1. 通过 `sub_14025F8A8` 获取分配器/构建上下文。
2. 准备 mount override 参数：
   - 默认 `v22 = 0`
   - 默认 `v23 = 0x100000`
   - 交给 `sub_1404099B0` 让外部覆盖。
3. 计算一个 clamp 后的偏移参数 `v11`（`max(0, v22 + *(a1+16))`）。
4. 调用 `sub_14065B67C(*(a2+8+248), *(a2+8), a3, v11, v23)` 构建 Tex 资源对象。
5. 成功则再包一层 24-byte wrapper（vft=`off_1436F7B68`），失败则回滚释放。

结论：`0x14065B540` 是 TexContext 的“入口 + 参数整形 + 结果包装”。

## 3. 0x14065B67C：创建/回退资源对象

逻辑：

1. 先走主路径：`sub_14065B6F8(*(a1+112), a4, a5, a6)`。
2. 若失败：
   - 仅在 `*(a1+2396) & 0x4000000` 置位时允许 fallback。
   - 优先用 `*(a1+6344)` 缓存对象；为空则调用虚函数 `(*(*a1+616))(a1)` 创建并缓存。
3. 最后构造 `{..., type=276, ...}` 描述，调用 `sub_140406D44` 统一封装为挂载资源对象。

结论：这是“主解析 + fallback + 通用对象封装”层。

## 4. 0x14065B6F8：读流头并进入块解析

流程：

1. 调 `sub_1405E9F0C(a1, stream, header)` 读取并校验头。
2. 若 `header[6] != 0`，会先调用流对象的 `vft+0x10` 做 skip（跳过前置区域）。
3. 用 `sub_1402AEAFC(a1, header[5?], header[5?], header[6?])` 创建目标容器对象。
4. 调 `sub_14065B7D8(..., header, flags=3, ...)` 进入分块读取。
5. 失败则释放容器对象。

`sub_1405E9F0C` 的头部判断要点：

- 先读固定长度 `qword_1442B3808 = 0x14`。
- `magic` 必须是 `0x47315447`。
- `version` 允许范围含 `0x30303230~0x30303630`（并额外允许 `0x30303530/0x30303430/0x30303330`等分支）。
- 新版本会再读额外 8 字节（`a3[5], a3[6]`），其中 `a3[5]` 只允许 `0/10/14`。

可用的头结构推断（字段名为语义名）：

```cpp
struct TexStreamHeader {
  uint32_t magic;          // 0x00, must be 0x47315447
  uint32_t version;        // 0x04, 0x3030xx30 family
  uint32_t unk08;          // 0x08
  uint32_t headerBytes;    // 0x0C, old=0x14, new path约为0x1C
  uint32_t blockCount;     // 0x10, 供 0x14065B7D8 循环
  uint32_t extMode;        // 0x14, only 0/10/14 (new versions)
  uint32_t preBlockSkip;   // 0x18, 进入块循环前先跳/读
};
```

## 5. 0x14065B7D8：分块读流分发器（关键）

它负责“按块读取 + 根据块类型分发到具体解码器”。

### 5.1 前置

1. 如果 `header.preBlockSkip` 非 0，先从流里消耗这段数据。
2. 循环 `header.blockCount` 次处理 block。
3. 每次先读 8 字节块头（`BlockHdr8`）。

### 5.2 块头分派

低 4 位 `type = blockHdr & 0xF`：

- `0` 或 `3` -> `sub_14065D864`（TexContext 主要走这里）
- `1` -> `sub_140DFA9F0`
- `2` -> 可先校验 `sub_1403F50F8`，再 `sub_1403F4C40`
- `4` -> 可先校验 `sub_140DFAEDC`，再 `sub_140DFAA8C`

### 5.3 扩展头

当 `(blockHdr.high_dword & 0xF0000000) != 0` 且 `version >= 0x30303630` 时：

1. 先读 12 字节扩展头。
2. 若扩展头声明长度 > 12，则继续读剩余参数区。
3. 该扩展头会影响后续 Tex 数据读取分支。

### 5.4 链式 payload 指针

若存在前置链表指针 `v15` 且 `blockHdr` 高位 nibble 标记了链数量，则会遍历子链并在对应节点打 active 标记（字节位写 0/1）。

这解释了它为何不仅读流，还要操作运行时链结构。

## 6. 0x14065D864：Tex 子分发（直达 0x14065BB40）

`sub_14065D864` 可以看成 Tex block 的二级分发器：

1. 若启用校验标志（`a9 & 1`），先调用 `sub_14065F348` 做合法性校验。
2. 若不要求真正构建（`(a9 & 2)==0`）则直接成功返回。
3. 若扩展头 flags 含 `0x20000`：
   - 先再读 36 字节头（首 DWORD 必须 `0x30303030`）
   - 走 `expandlocale(...)` 特殊路径。
4. 否则走常规纹理构建：`sub_14065BB40(...)`。

## 7. 0x14065BB40：TexContext 真正读 payload 与组装资源

这个函数是“真实数据流读取与纹理资源构建”的主体。

主流程拆解：

1. 从 block 头字节解析像素格式/布局信息（`sub_1401052D8` + `unk_143DF74E8`）。
2. 根据格式与维度计算每 mip 层/每 plane 的读取字节量。
3. 若存在预跳 mip（`a3+0x40` 字节字段），先对 stream 做多次 skip。
4. 从目标对象（`a3`）取纹理描述（维度、mip 数、array 层数等）。
5. 分配子资源布局内存：`subresourceCount * 0xA4`。
6. 调设备/资源管理器接口生成每个 subresource 的 offset/rowPitch/slicePitch 映射。
7. 创建 staging/upload 资源并映射。
8. 双层循环（array * mip）读取流数据：
   - 普通路径：直接 read 到目标布局。
   - 分块/对齐路径（`a3[8] & 0x200`）：先读到临时块缓存，再按行搬运 `memmove` 到目标。
9. 写回每个 subresource 描述项（索引、布局指针、上下文句柄）。
10. 提交到资源系统：`sub_140CC688C(...)`。
11. 失败路径清理：unmap、释放临时缓存、释放中间对象。

结论：你提到的“真正 payload 读取不在挂载主函数，而在 handler 内”在 TexContext 路径上是成立的，关键读流就在 `0x14065BB40`。

## 8. 推荐重命名（便于后续继续逆）

- `0x14065B540` -> `TexContext_MountHandler`
- `0x14065B67C` -> `TexContext_BuildRuntimeObject`
- `0x14065B6F8` -> `TexContext_ParseStreamHeaderAndLoad`
- `0x14065B7D8` -> `TexContext_ReadBlockStream`
- `0x14065D864` -> `TexContext_DispatchBlockPayload`
- `0x14065BB40` -> `TexContext_LoadPayloadAndBuildTexture`
- `0x1405E9F0C` -> `TexStream_ReadAndValidateHeader`


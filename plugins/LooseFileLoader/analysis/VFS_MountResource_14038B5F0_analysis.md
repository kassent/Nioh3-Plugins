# Nioh3 `VFS_MountResource_14038B5F0` Resource Loading Analysis

## 1. Scope and conclusion

- Target function: `0x14038B5F0` (`VFS_MountResource_14038B5F0`)
- Conclusion: this is the core runtime resource mount/instantiate function. It takes a resolved RDB entry + stream context, builds create descriptors, selects a type handler by hash, constructs the resource object, then commits callbacks/state or logs failure.

High-level path:

1. Build/normalize input reader wrappers
2. Select optional decode path from header mode bits
3. Build temporary create context and parameter descriptors
4. Resolve resource type handler by `TypeHash`
5. Call handler virtual create method
6. On success attach callbacks; on failure log `Name + Hash`

---

## 2. Mapping to `rdb/rdx/fdata`

Using your `RDBReader.cs` / `RDXReader.cs` plus sample package files:

- `root.rdx` layout is 8-byte records: `Index(u16), Marker(u16), FileId(u32)`.
- `root.rdb` `IDRK` entries contain location metadata:
  - `DataSize == 0x0D`: 32-bit offset
  - `DataSize == 0x11`: high8 + low32 offset
  - `FDataId` maps via `rdx[Index] -> FileId -> 0xXXXXXXXX.fdata`

In game code, the same concepts appear in/around `0x14038B5F0`:

- `*(u32*)(a7 + 8)` is resource type hash (matches your hook usage for type classification)
- `*(u32*)(a8 + 40)` and `*(u32*)(a8 + 48)` are mode/param-count fields from an `IDRK`-like header
- `Rdb_FindIndexByKtid_1413F13F0(...)` provides the final hash/index used for logging and lookup

---

## 3. Detailed flow of `0x14038B5F0`

## 3.1 Reader setup

Early logic:

- `mode = (*(u32*)(a8 + 40) >> 20) & 0x3F`
- If `mode == 0`, wrap/copy reader through `sub_140C6F5D8` into local wrapper object
- If `a6 != 0`, add another wrapper via `sub_1430CEAE8`

This normalizes reader interfaces before actual resource creation.

## 3.2 Optional decode adapter switch

If `a14 != 0 && a12 != 0`, function may switch adapter based on `mode`:

- `mode == 1`: `sub_1410D0EE0`
- `mode == 3`: `sub_1430D8D0C`
- `mode == 4`: virtual call `(*v23)[6]`

If adapter init fails:

- release current reader
- update event/state via `sub_14007EBD0` (when `a2 != 0`)
- return failure

## 3.3 Context + buffer preparation

Temporary buffer size:

- `alloc_size = 24 + 32 * *(u32*)(a8 + 48)`
- stack alloca for small buffers, pool allocator fallback for larger sizes

Then state migration/reset on `a7`:

- move `a7+32/a7+36` into `a7+24/a7+28`
- clear `a7+32` and status bits
- atomically detach pending callback handle from `a7+48`

## 3.4 Build create descriptors

Call sequence:

- `sub_14038C388(...)` packs base location tuple from `a7`
- `sub_14038C344(...)` builds create context object
- `sub_14038A594(a3, *(u32*)(a7+8))` resolves type handler by hash
- `sub_14038C1A4(...)` expands parameter descriptors from header param table

`sub_14038A594` is the key hash-to-handler lookup function.

## 3.5 Instantiate resource

Core create call:

- `obj = handler->vfunc_0xB0(createCtx, mountCtx, desc)`
- save object to `*(u64*)(a7 + 16)`

Post-create:

- cleanup temporary mount context via `sub_14038B050`
- optional state update via `sub_14007EBD0`

## 3.6 Success/failure branches

Success (`obj != 0`):

- if detached callback handle exists:
  - call callback directly if callback target already set
  - else register through `sub_140920B60(a3, [callback, a7])`
- return `1`

Failure (`obj == 0`):

- resolve name through `sub_14026F4A4(a7, ...)`
- resolve hash/index through `Rdb_FindIndexByKtid_1413F13F0(...)`
- log `"can't create resource Name[%s] Hash[0x%08x]"`
- return `0`

Final mandatory cleanup:

- `sub_14038C41C(...)`
- `sub_14038C3F4(...)`

---

## 4. Parent (caller) analysis

Direct callers of `0x14038B5F0`:

## 4.1 `sub_14038DAEC` (main dispatcher parent)

Role:

- pulls stream/mode/context from upper task object
- assembles wrapper arguments (`a9/a12` path)
- calls `VFS_MountResource_14038B5F0`
- releases references on exit

This is the primary parent function in normal mount path.

## 4.2 `sub_14062B724` (pre-read then mount)

Role:

- computes index using `sub_1405881A0(...)`
- builds path/header info via `sub_14062C304`
- preloads data into memory via `sub_14026FBA0`
- then calls `VFS_MountResource_14038B5F0`

## 4.3 `sub_1430D759C` (async bridge parent)

Role:

- allocates temp buffer
- reads stream segment
- calls `VFS_MountResource_14038B5F0`
- releases temp buffer

Usually wrapped by `sub_1430D6B1C` in task/parallel path.

## 4.4 Thin API wrappers

- `sub_14229D178`: direct pass-through call
- `sub_14229D1EC`: builds default reader-vtable object then calls mount

## 4.5 Important higher-level parents

- `sub_140BAB148` / `sub_140BAB1B4` -> `sub_14038DAEC` -> `VFS_MountResource`
- `sub_1430D6B1C` -> `sub_1430D759C` -> `VFS_MountResource`
- `sub_14229D290` (decrypt/verify path) -> `sub_14229D178` -> `VFS_MountResource`
- `sub_14038CD58` is a major orchestrator:
  - acquires stream
  - builds/validates `IDRK` header blocks
  - prepares memory/path context
  - dispatches into the above bridge callers

---

## 5. Child (callee) analysis

Key callees used by `0x14038B5F0`:

## 5.1 Reader/filter path

- `sub_140C6F5D8`: base reader wrapper init/copy
- `sub_1430CEAE8`: extended wrapper init
- `sub_1410D0EE0`: mode-1 reader init
- `sub_1430D8D0C`: mode-3 reader init

## 5.2 Type/index path

- `sub_14038A594`: type hash lookup to type handler object
- `Rdb_FindIndexByKtid_1413F13F0`: KTID to final index/hash
- `sub_1405881A0`: offset-to-index mapping utility used by index path

## 5.3 Descriptor construction path

- `sub_14038C388`: build base location pair
- `sub_14038C344`: build create context object
- `sub_14038C1A4`: expand param descriptors into temp array

## 5.4 Cleanup/state path

- `sub_14038B050` / `sub_14038B10C`: destroy mount/create temp objects
- `sub_14038C41C` / `sub_14038C3F4`: destroy wrapper objects
- `sub_14026F4A4`: fetch resource name for error logs
- `sub_14007EBD0`: state/event flag update

---

## 6. Stable offset cheat sheet

Observed stable offsets from disassembly:

- `a8 + 0x28` (`+40`): mode/compression bitfield (`>>20 & 0x3F`)
- `a8 + 0x30` (`+48`): parameter count
- `a8 + 0x18` (`+24`): size/position field used in mount context

- `a7 + 0x08`: resource type hash
- `a7 + 0x10`: output resource object slot
- `a7 + 0x18/+0x1C`: runtime location/state fields
- `a7 + 0x20/+0x24`: source location fields moved then cleared
- `a7 + 0x30`: pending callback handle (detached with atomic exchange)

---

## 7. End-to-end runtime sequence

1. Parent orchestrators (`sub_14038CD58` etc.) resolve RDB/RDX/fdata context and prepare reader/header
2. Bridge parent (`sub_14038DAEC`, `sub_1430D759C`, etc.) invokes `0x14038B5F0`
3. `0x14038B5F0` chooses reader/decode path from mode bits
4. Build create context + param descriptors
5. Resolve type handler by hash and call create vfunc
6. Success: commit object + callbacks
7. Failure: log name/hash failure
8. Always release temp readers/wrappers/buffers

---

## 8. Practical plugin implications

- Your current hook point is correct. `a7+8` is a stable type hash source.
- To map exact `fdata` block + offset earlier, instrument parent chain (`sub_14038CD58`, `sub_14062B724`) where index/path info is still explicit.
- For loose-file override, practical interception points are:
  - return value of `Rdb_FindIndexByKtid_1413F13F0`
  - read path `sub_14026FBA0`
  - pre-create descriptor buffer before `handler->vfunc_0xB0`


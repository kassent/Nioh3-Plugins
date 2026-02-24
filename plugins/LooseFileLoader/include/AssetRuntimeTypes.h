#pragma once

#include "Relocation.h"

#include <cstdint>
#include <string>

namespace LooseFileLoader {

#pragma pack(push, 1)
struct RdbRuntimeEntryDesc {
    char magic[4];        // +0x00, "IDRK" (0x14062BBF8 初始化 + 0x140270A44读取)
    char version[4];      // +0x04
    uint64_t sizeInContainer;    // +0x08, allBlockSize in rdata sectionBytes
    uint64_t compressedSize; // +0x10
    uint64_t fileSize;     // +0x18 uncompressedSize in rdata
    uint32_t fileKtid;     // +0x20
    uint32_t typeInfoKtid; // +0x24, 0x14038C1A4 用于 TypeHandler 查找
    uint32_t flags;        // +0x28, 0x14038B5F0: (flags>>20)&0x3F 压缩模式
    uint32_t f2C;          // +0x2C
    uint32_t paramCount; // +0x30, 0x14038C1A4 使用
    uint32_t f34;          // +0x34, 当前路径未稳定使用
    void* paramDataBlock;  // +0x38

    std::string GetMagic() const {
        return std::string(magic, 4);
    }

    std::string GetVersion() const {
        return std::string(version, 4);
    }
};
static_assert(sizeof(RdbRuntimeEntryDesc) == 0x40);

struct GameAsset {
    std::uint32_t stateFlags = 0;      // +0x00 runtime state bits (load/create/pending)
    std::uint32_t refFlags = 0;        // +0x04 runtime status flags
    std::uint32_t typeInfoKtid = 0;    // +0x08 resource type id/hash
    std::uint32_t reserved0C = 0;      // +0x0C
    std::uint64_t objectPtr = 0;       // +0x10 created runtime object (shell), may be null
    std::uint64_t paramDataBlock = 0;  // +0x18
    std::uint32_t rangeBegin = 0;      // +0x20
    std::uint32_t rangeSize = 0;       // +0x24
    std::uint32_t fileKtid = 0;        // +0x28
    std::uint32_t reserved24 = 0;      // +0x2C
    std::uint32_t flags = 0;           // +0x30 RDB/RIS flags (compression mode in high bits)
    std::uint32_t paramCount = 0;      // +0x34
};
static_assert(sizeof(GameAsset) == 0x38);
#pragma pack(pop)



class IAssetStreamReader {
public:
    virtual ~IAssetStreamReader() = default;
    virtual void Close() = 0;
    virtual std::int64_t Skip(std::int64_t deltaBytes) = 0;
    virtual std::uint64_t ReadByte(std::uint8_t* outByte) = 0;
    virtual std::uint64_t Read(void* dst, std::uint64_t dstOffset, std::uint64_t size) = 0;
    virtual std::uint64_t QueryCapability() = 0;
};

class IBaseGameAssetHandler {
public:
    virtual void Unk00() = 0;
    virtual void Unk08() = 0;
    virtual void Unk10() = 0;
    virtual const char*& GetTypeName(const char*& typeNameOut) const = 0;
    virtual std::uint32_t GetTypeID() = 0;
    virtual void Unk28() = 0;
    virtual void Unk30() = 0;
    virtual void Unk38() = 0;
    virtual void Unk40() = 0;
    virtual void Unk48() = 0;
    virtual void Unk50() = 0;
    virtual void Unk58() = 0;
    virtual void Unk60() = 0;
    virtual void Unk68() = 0;
    virtual void Unk70() = 0;
    virtual void Unk78() = 0;
    virtual void Unk80() = 0;
    virtual void Unk88() = 0;
    virtual void Unk90() = 0;
    virtual void Unk98() = 0;
    virtual void UnkA0() = 0;
    virtual void UnkA8() = 0;
    virtual void* Deserialize(void* param1, IAssetStreamReader* reader, void* param3) = 0;

    inline std::uintptr_t GetVtableAddr() const {
        return *reinterpret_cast<const std::uintptr_t*>(this) - RelocationManager::s_baseAddr + 0x140000000ull;
    }

    inline std::string GetTypeName() const {
        const char* typeNamePtr = nullptr;
        GetTypeName(typeNamePtr);
        return typeNamePtr ? typeNamePtr : "Unknown";
    }
};

}  // namespace LooseFileLoader


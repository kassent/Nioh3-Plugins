#pragma once

#include "Relocation.h"

#include <cstdint>
#include <string>

#define PLUGIN_NAME "LooseFileLoader"
#define PLUGIN_VERSION_MAJOR 1
#define PLUGIN_VERSION_MINOR 1
#define PLUGIN_VERSION_PATCH 0

namespace LooseFileLoader {

inline bool g_enableAssetLoadingLog = false;

#pragma pack(push, 1)
struct RDBDescriptor {
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
static_assert(offsetof(RDBDescriptor, magic) == 0x00);
static_assert(offsetof(RDBDescriptor, version) == 0x04);
static_assert(offsetof(RDBDescriptor, sizeInContainer) == 0x08);
static_assert(offsetof(RDBDescriptor, compressedSize) == 0x10);
static_assert(offsetof(RDBDescriptor, fileSize) == 0x18);
static_assert(offsetof(RDBDescriptor, fileKtid) == 0x20);
static_assert(offsetof(RDBDescriptor, typeInfoKtid) == 0x24);
static_assert(offsetof(RDBDescriptor, flags) == 0x28);
static_assert(sizeof(RDBDescriptor) == 0x40);

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


struct GameManager {
    struct AssetIdManager {
        uint32_t *sortedFileKtid;                 // +0x08, passed as (assetManager + 0x08)   // 218 offset from archiveManager
        uint32_t *collectedFileKtid;              // +0x10
        uint32_t keyCount;                        // +0x18
        uint32_t keyCountCopy;                    // +0x1C
        uint32_t treeDepth;                       // +0x20
        uint32_t stepPerDepth;                    // +0x24
        uint32_t branchFactor;                    // +0x28
        uint32_t branchSpan;                      // +0x2C
        uint32_t bucketStrideBytes;               // +0x30
        uint32_t unk34;                           // +0x34

        DEF_MEMBER_FN_REL_CONST(GetResIdByFileKtid, uint32_t, 0x0773EAC, "E8 ? ? ? ? 8B D0 48 8B CB E8 ? ? ? ? BB", 0, 1, 5, uint32_t a_fileKtid);

        DEF_MEMBER_FN_REL_CONST(GetResItemById, GameAsset *, 0x09CF148, "E8 ? ? ? ? 49 8B 95 ? ? ? ? 4C 8B C0 E8", 0, 1, 5, uint32_t a_resId);

        DEF_MEMBER_FN_REL_CONST(GetResIDFromRes, uint32_t, 0x0591960, "E8 ? ? ? ? 45 33 E4 85 DB", 0, 1, 5, GameAsset * a_res);

        DEF_MEMBER_FN_REL_CONST(GetFileKtIdFromRes, uint32_t, 0x13F5A80, "E8 ? ? ? ? 49 8D AF ? ? ? ? 8B D0", 0, 1, 5, GameAsset * a_res);
    };
    struct AssetManager {
        // BuildResourceObjectsFromRdb_140215F44 (0x140215F44) mapped fields.
        uint8_t unk00[0x08];                      // +0x00
        AssetIdManager assetIdManager;            // +0x08
        void* compressedResFileIdBitset;          // +0x38
        void* mountListener;                      // +0x40, vtbl calls +0x40/+0x48/+0x58 in 140215F44
        void* rdbSectionDescArray;                // +0x48
        void** rdbSectionFileHandleArray;         // +0x50
        char* rdbSectionPathBuffer;               // +0x58
        void* fileKtidToResFileIdLookup;          // +0x60
        void* runtimeBufferOwner;                 // +0x68
        void* runtimeBuffer;                      // +0x70
        uint8_t unk78[0x10];                      // +0x78
        void* type2LoadContext;                   // +0x88
        void* type10LoadContext;                  // +0x90
        void* externalPayloadContext;             // +0x98
        uint8_t unkA0[0x10];                      // +0xA0
        void* mountListenerArg;                   // +0xB0
        uint8_t unkB8[0x10];                      // +0xB8
        volatile uint32_t dbFlags200;             // +0xC8
        uint32_t unkCC;                           // +0xCC

        bool IsRdbReady() const { return (dbFlags200 & 0x40000000u) != 0; }
        uint32_t GetSectionCount() const { return dbFlags200 & 0x1Fu; }
    };
    static_assert(offsetof(AssetManager, assetIdManager) == 0x08);
    static_assert(offsetof(AssetManager, compressedResFileIdBitset) == 0x38);
    static_assert(offsetof(AssetManager, mountListener) == 0x40);
    static_assert(offsetof(AssetManager, rdbSectionDescArray) == 0x48);
    static_assert(offsetof(AssetManager, rdbSectionFileHandleArray) == 0x50);
    static_assert(offsetof(AssetManager, rdbSectionPathBuffer) == 0x58);
    static_assert(offsetof(AssetManager, fileKtidToResFileIdLookup) == 0x60);
    static_assert(offsetof(AssetManager, runtimeBufferOwner) == 0x68);
    static_assert(offsetof(AssetManager, runtimeBuffer) == 0x70);
    static_assert(offsetof(AssetManager, type2LoadContext) == 0x88);
    static_assert(offsetof(AssetManager, type10LoadContext) == 0x90);
    static_assert(offsetof(AssetManager, externalPayloadContext) == 0x98);
    static_assert(offsetof(AssetManager, mountListenerArg) == 0xB0);
    static_assert(offsetof(AssetManager, dbFlags200) == 0xC8);

    struct ArchiveManager {
        uint8_t unk00[0x210];
        AssetManager assetManager;  // +0x210

        DEF_MEMBER_FN_REL_CONST(GetResHandlerFromType, void *, 0x0183E5C, "E8 ? ? ? ? 8B 76 ? 4C 8B E0", 0, 1, 5, uint32_t a_typeInfoKtid);
    };
    static_assert(offsetof(ArchiveManager, assetManager) == 0x210);

    uint8_t unk00[0x530];
    ArchiveManager* archiveManager;  // +0x530

    AssetManager* GetAssetManager() const {
        return archiveManager ? &archiveManager->assetManager : nullptr;
    }
};
static_assert(offsetof(GameManager, archiveManager) == 0x530);
inline REL::Relocation<GameManager**> g_gameMain(REL::Pattern(0x4566990, "48 8B 05 ? ? ? ? 44 0F 28 D3", 0, 3, 7));

#pragma pack(pop)


class IFileStreamReader {
public:
    virtual ~IFileStreamReader() = default;
    virtual void Close() = 0;
    virtual std::int64_t Skip(std::int64_t deltaBytes) = 0;
    virtual std::uint64_t ReadByte(std::uint8_t* outByte) = 0;
    virtual std::uint64_t Read(void* dst, std::uint64_t dstOffset, std::uint64_t size) = 0;
    virtual std::uint64_t GetID() const = 0;
};


class AssetReader : public IFileStreamReader {
public:
    using ArchiveManager = GameManager::ArchiveManager;
    ArchiveManager*     archiveManager; // +0x08
    IFileStreamReader * streamReader;  // +0x10
    uint64_t            archiveFileHandle; // +0x18
    uint64_t            archiveFileOffset; // +0x20
    uint64_t            assetFileSize; // +0x28

    struct ArchiveInfo {
        uint64_t field00[0x28 >> 3]; // +0x00
        char     filePath[512]; // +0x28
    };
    static_assert(offsetof(ArchiveInfo, filePath) == 0x28);

    DEF_MEMBER_FN_REL_CONST(GetArchiveInfo, bool, 0x05E8C50, "E8 ? ? ? ? 85 C0 0F 85 ? ? ? ? 4C 8B 7E", 0, 1, 5, ArchiveInfo * a_archiveInfo);
};
static_assert(offsetof(AssetReader, archiveManager) == 0x08);
static_assert(offsetof(AssetReader, streamReader) == 0x10);
static_assert(offsetof(AssetReader, archiveFileHandle) == 0x18);
static_assert(offsetof(AssetReader, archiveFileOffset) == 0x20);
static_assert(offsetof(AssetReader, assetFileSize) == 0x28);
static_assert(sizeof(AssetReader) == 0x30);


struct AssetLoadingContext {
    using ArchiveManager = GameManager::ArchiveManager;
    uint64_t filed00; // +0x00
    ArchiveManager* archiveManager; // +0x08
    uint64_t filed10; // +0x10
    uint64_t filed18; // +0x18
    uint64_t filed20; // +0x20
    GameAsset* gameAsset; // +0x28
    uint64_t archiveFileHandle; // +0x30
};
static_assert(offsetof(AssetLoadingContext, archiveManager) == 0x08);
static_assert(offsetof(AssetLoadingContext, gameAsset) == 0x28);
static_assert(offsetof(AssetLoadingContext, archiveFileHandle) == 0x30);

class IBaseGameAssetHandler {
public:
    struct ObjectField {
        uint32_t typeFlags = 0;
        int32_t  nameHash = 0;
        const char *name = nullptr;
        const void *format = nullptr;
    };

    virtual void Unk00() = 0;
    virtual void Unk08() = 0;
    virtual uint32_t ResolveFields(ObjectField* fieldsOut, uint32_t maxFields, uint32_t startFieldIndex) = 0;
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
    virtual void* Deserialize(AssetLoadingContext* loadingContext, IFileStreamReader* reader, void* param3) = 0;

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


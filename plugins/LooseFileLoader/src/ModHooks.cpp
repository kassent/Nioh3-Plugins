#include "ModHooks.h"
#include "Common.h"
#include "ModFileReader.h"
#include "ModAssetManager.h"

#include <HookUtils.h>
#include <LogUtils.h>

#include <filesystem>

#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace LooseFileLoader {
namespace {
using ArchiveManager = GameManager::ArchiveManager;
using FnLoadAssetFromFile =
    bool (*)(std::uintptr_t, std::uintptr_t, ArchiveManager*, void*, void*, std::uintptr_t,
        GameAsset*, RdbRuntimeEntryDesc*, IAssetStreamReader*,
        std::uintptr_t, std::uintptr_t, void*, void*, bool);

using FnRegisterAssetHandler = bool (*)(void*, std::uint32_t, IBaseGameAssetHandler*);

std::string FormatDiskSize(std::uint64_t size) {
    constexpr double kUnit = 1024.0;
    constexpr const char* kUnits[] = {"B", "KB", "MB", "GB", "TB"};

    double value = static_cast<double>(size);
    std::size_t unitIndex = 0;
    while (value >= kUnit && unitIndex < (std::size(kUnits) - 1)) {
        value /= kUnit;
        ++unitIndex;
    }

    if (unitIndex == 0) {
        return std::to_string(size) + "B";
    }

    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.1f%s", value, kUnits[unitIndex]);
    return buffer;
}


std::optional<bool> TryLoadAssetOverride(
    FnLoadAssetFromFile     original,
    std::uintptr_t          param1, 
    std::uintptr_t          param2, 
    ArchiveManager*         archiveManager,
    void*                   mountCallerCtx, 
    void*                   mountCallbackCtx, 
    std::uintptr_t          param6,
    GameAsset*              targetAsset, 
    RdbRuntimeEntryDesc*    rdbEntryDesc, 
    IAssetStreamReader*     assetStreamReader,
    std::uintptr_t          payloadDataOffset, 
    std::uintptr_t          payloadSize, 
    void*                   decompressor,
    void*                   handlerUserCtx, 
    bool                    requireDecompress
) {

    if (archiveManager == nullptr || targetAsset == nullptr || rdbEntryDesc == nullptr) {
        return std::nullopt;
    }

    const auto fileId = rdbEntryDesc->fileKtid;
    if (fileId == 0xFFFFFFFF) {
        return std::nullopt;
    }

    const auto typeId = rdbEntryDesc->typeInfoKtid;
    auto* assetHandler = (IBaseGameAssetHandler*)archiveManager->GetResHandlerFromType(typeId);
    const std::string typeName = assetHandler ? assetHandler->GetTypeName() : "Unknown";

    if (g_enableAssetLoadingLog) {
        _MESSAGE("Loading asset: 0x%08X | Type: %s (0x%08X) | Size: %s", fileId, typeName.c_str(), typeId, FormatDiskSize(rdbEntryDesc->fileSize).c_str());
    }

    // auto resId = archiveManager->assetManager.assetIdManager.GetResIdByFileKtid(fileId);
    // _MESSAGE("ResId: %u, fileId: %08X, typeId: %08X", resId, fileId, typeId);

    // auto *resItem = archiveManager->assetManager.assetIdManager.GetResItemById(resId);
    // _MESSAGE("ResItem: %p, targetAsset: %p", resItem, targetAsset);
    const auto overridePath = g_modAssetManager.Find(fileId);
    if (!overridePath.has_value()) {
        return std::nullopt;
    }
    
    std::error_code ec;
    if (!fs::exists(*overridePath, ec) || !fs::is_regular_file(*overridePath, ec)) {
        return std::nullopt;
    }

    auto reader = std::make_unique<ModFileReader>(*overridePath);
    if (!reader->IsOpen()) {
        _MESSAGE("Failed to open mod asset file: %s", overridePath->string().c_str());
        return std::nullopt;
    }


    const auto oldFlags = rdbEntryDesc->flags;
    rdbEntryDesc->flags = oldFlags | 0x100000;

    const bool result = original(
        param1, param2, archiveManager, mountCallerCtx, mountCallbackCtx, param6,
        targetAsset, rdbEntryDesc, reader.get(), payloadDataOffset, payloadSize,
        nullptr, handlerUserCtx, false);
    if (result) {
        _MESSAGE("Loaded mod asset successfully: 0x%08X | Type: %s (0x%08X) | %s",
                fileId, typeName.c_str(), typeId, overridePath->string().c_str());
    } else {
        _MESSAGE("Failed to load mod asset: 0x%08X | Type: %s (0x%08X) | %s",
                fileId, typeName.c_str(), typeId, overridePath->string().c_str());
    }
    rdbEntryDesc->flags = oldFlags;
    return result;
}

#ifdef _DEBUG
bool InstallRegisterAssetHandlerHook() {
    auto registerAssetHandler = reinterpret_cast<FnRegisterAssetHandler>(
        HookUtils::ScanIDAPattern("E8 ? ? ? ? 84 C0 0F 84 ? ? ? ? 83 65 ? 00 48 8D 15", 0, 1, 5));
    if (registerAssetHandler == nullptr) {
        _MESSAGE("Failed to resolve RegisterAssetHandler");
        return false;
    }
    _MESSAGE("RegisterAssetHandler: %p", registerAssetHandler);

    HookLambda(registerAssetHandler, [](void* archiveManager, std::uint32_t assetHash,
                                      IBaseGameAssetHandler* assetHandler) -> bool {
        char buf[256] = {0};
        if (assetHandler) {
            do {
                __try {
                    const char* typeName = nullptr;
                    assetHandler->GetTypeName(typeName);
                    if (typeName) {
                        snprintf(buf, sizeof(buf), "%s", typeName);
                    } else {
                        snprintf(buf, sizeof(buf), "Unknown");
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    break;
                }
                _MESSAGE("Asset ID: 0x%08X | %p | %s ",
                    assetHash, assetHandler->GetVtableAddr(), buf);
            } while (0);
        }

        return original(archiveManager, assetHash, assetHandler);
    });
    return true;
}
#endif

}  // namespace

bool InstallLoadAssetFromFileHook() {
    auto loadAssetFromFile = reinterpret_cast<FnLoadAssetFromFile>(
        HookUtils::ScanIDAPattern("E8 ? ? ? ? 48 8B 8F ? ? ? ? 48 8B 53", 0, 1, 5));
    if (loadAssetFromFile == nullptr) {
        _MESSAGE("Failed to resolve LoadAssetFromFile");
        return false;
    }
    _MESSAGE("LoadAssetFromFile: %p", loadAssetFromFile);

    HookLambda(loadAssetFromFile, [](
        std::uintptr_t          param1, 
        std::uintptr_t          param2, 
        ArchiveManager*         archiveManager,
        void*                   mountCallerCtx, 
        void*                   mountCallbackCtx, 
        std::uintptr_t          param6,
        GameAsset*              targetAsset, 
        RdbRuntimeEntryDesc*    rdbEntryDesc, 
        IAssetStreamReader*     assetStreamReader, 
        std::uintptr_t          payloadDataOffset, 
        std::uintptr_t          payloadSize, 
        void*                   decompressor, 
        void*                   handlerUserCtx, 
        bool                    requireDecompress
    ) -> bool {
        if (const auto overrideResult = 
            TryLoadAssetOverride(
                original, 
                param1, 
                param2, 
                archiveManager, 
                mountCallerCtx, 
                mountCallbackCtx, 
                param6,
                targetAsset, 
                rdbEntryDesc, 
                assetStreamReader, 
                payloadDataOffset, 
                payloadSize,
                decompressor, 
                handlerUserCtx, 
                requireDecompress
            ); overrideResult.has_value()) {
            return *overrideResult;
        }

        return original(
            param1, 
            param2, 
            archiveManager, 
            mountCallerCtx, 
            mountCallbackCtx, 
            param6, 
            targetAsset, 
            rdbEntryDesc, 
            assetStreamReader, 
            payloadDataOffset, 
            payloadSize, 
            decompressor, 
            handlerUserCtx, 
            requireDecompress
        );
    });

    return true;
}


bool InstallHooks() {
    if (!InstallLoadAssetFromFileHook()) {
        _MESSAGE("Failed to install LoadAssetFromFile hook");
        return false;
    }
#ifdef _DEBUG
    if (!InstallRegisterAssetHandlerHook()) {
        _MESSAGE("Failed to install RegisterAssetHandlerHook");
        return false;
    }
#endif
    return true;

}

}  // namespace LooseFileLoader

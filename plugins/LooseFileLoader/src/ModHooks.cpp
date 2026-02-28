#include "ModHooks.h"
#include "Common.h"
#include "ModFileReader.h"
#include "ModAssetManager.h"

#include <HookUtils.h>
#include <LogUtils.h>

#include <cstdint>
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
        GameAsset*, RDBDescriptor*, IFileStreamReader*,
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



bool InstallGetArchiveInfoFromAssetLoaderHook() {
    using FnGetArchiveInfo = int32_t (*)(AssetReader*, AssetReader::ArchiveInfo*);
    auto func = (FnGetArchiveInfo)HookUtils::ScanIDAPattern("E8 ? ? ? ? 85 C0 0F 85 ? ? ? ? 4C 8B 7E", 0, 1, 5);
    if (func == nullptr) {
        _MESSAGE("Failed to resolve GetArchiveInfo");
        return false;
    }

    HookLambda(func, [](AssetReader* assetReader, AssetReader::ArchiveInfo* archiveInfo) -> int32_t {
        int32_t errorCode = original(assetReader, archiveInfo);

        // _MESSAGE("GetArchiveInfo result: %d, fileHandle: %p, path: %s", errorCode, assetReader->archiveFileHandle, archiveInfo->filePath);
        auto *streamReader = assetReader->streamReader;
        if (errorCode == 0 && streamReader != nullptr && streamReader->GetID() == ModFileReader::kModFileReaderId) {
            auto *modFileReader = (ModFileReader*)streamReader;
            std::string vanillaFilePath = archiveInfo->filePath;
            auto filePath = modFileReader->GetFilePath();
            std::strncpy(archiveInfo->filePath, filePath.c_str(), filePath.size());
            archiveInfo->filePath[filePath.size()] = '\0';
            _MESSAGE("Redirect streaming file path: %s -> %s", vanillaFilePath.c_str(), filePath.c_str());
        }
        return errorCode;
    });
    return true;
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
        if (assetHandler) {
            do {
                std::string handlerTypeName = assetHandler->GetTypeName();
                _MESSAGE("--------------------------------");
                _MESSAGE("Asset ID: 0x%08X | %p | %s ",
                    assetHash, assetHandler->GetVtableAddr(), handlerTypeName.c_str());
                
                static std::unique_ptr<IBaseGameAssetHandler::ObjectField[]> fields = std::make_unique<IBaseGameAssetHandler::ObjectField[]>(1024);
                const uint32_t fieldCount = assetHandler->ResolveFields(fields.get(), 1024, 0);
                for (uint32_t i = 0; i < fieldCount; ++i) {
                    _MESSAGE("    %d: %s(%08X) %08X", i, fields[i].name, fields[i].nameHash, fields[i].typeFlags);
                }
            } while (0);
        }

        return original(archiveManager, assetHash, assetHandler);
    });
    return true;
}
#endif

}  // namespace


bool InstallDeserializeAssetHook() {
    auto patchAddress = HookUtils::ScanIDAPattern("FF 93 B0 ? ? ? 48 8D 4D ? 49 89 45");
    if (patchAddress == 0) {
        _MESSAGE("Failed to resolve DeserializeAsset");
        return false;
    }
    // _MESSAGE("DeserializeAsset: %p", patchAddress);

    // FF 93 B0 00 00 00
    uint8_t nopBytes[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    HookUtils::SafeWriteBuf(patchAddress, nopBytes, sizeof(nopBytes));

    static auto deserializeAssetMidHook = safetyhook::create_mid(patchAddress, [](SafetyHookContext& ctx) {
        auto *assetHandler = (IBaseGameAssetHandler*)ctx.rcx;
        auto *loadingContext = (AssetLoadingContext*)ctx.rdx;
        auto *assetReader = (AssetReader*)ctx.r8;
        auto *archiveManager = assetReader->archiveManager;
        auto *gameAsset = loadingContext->gameAsset;
        auto assetFileSize = assetReader->assetFileSize;

        // _MESSAGE("assetHandler: %p, loadingContext: %p, assetReader: %p, archiveManager: %p, gameAsset: %p, assetFileSize: %llu",
        //         assetHandler, loadingContext, assetReader, archiveManager, gameAsset, assetFileSize);

        do {
            if (gameAsset == nullptr || archiveManager == nullptr) {
                break;
            }
            auto fileKtid = archiveManager->assetManager.assetIdManager.GetFileKtIdFromRes(gameAsset);
            if (fileKtid == 0xFFFFFFFF) {
                break;
            }
            const auto typeId = gameAsset->typeInfoKtid;
            const std::string typeName = assetHandler ? assetHandler->GetTypeName() : "Unknown";
        
            if (g_enableAssetLoadingLog) {
                _MESSAGE("\tLoading asset: 0x%08X | Type: %s (0x%08X) | Size: %s", fileKtid, typeName.c_str(), typeId, FormatDiskSize(assetFileSize).c_str());
            }
            // auto resId = archiveManager->assetManager.assetIdManager.GetResIdByFileKtid(fileKtid);
            // _MESSAGE("ResId: %u, fileKtid: %08X, typeId: %08X", resId, fileKtid, typeId);

            // auto *resItem = archiveManager->assetManager.assetIdManager.GetResItemById(resId);
            // _MESSAGE("ResItem: %p, targetAsset: %p", resItem, targetAsset);
            const auto overridePath = g_modAssetManager.Find(fileKtid);
            if (!overridePath.has_value()) {
                break;
            }

            std::error_code ec;
            if (!fs::exists(*overridePath, ec) || !fs::is_regular_file(*overridePath, ec)) {
                break;
            }

            auto reader = std::make_unique<ModFileReader>(*overridePath);
            if (!reader->IsOpen()) {
                _MESSAGE("Failed to open mod asset file: %s", overridePath->string().c_str());
                break;
            }

            assetReader->streamReader = reader.get();
            assetReader->assetFileSize = reader->GetFileSize();
            assetReader->archiveFileOffset = 0;

            auto *assetData = assetHandler->Deserialize(loadingContext, assetReader, (void*)ctx.r9);

            if (assetData) {
                _MESSAGE("Loaded mod asset successfully: 0x%08X | Type: %s (0x%08X) | %s",
                        fileKtid, typeName.c_str(), typeId, overridePath->string().c_str());
            } else {
                _MESSAGE("Failed to load mod asset: 0x%08X | Type: %s (0x%08X) | %s",
                        fileKtid, typeName.c_str(), typeId, overridePath->string().c_str());
            }

            ctx.rax = (uintptr_t)assetData;
            return;

        } while (false);

        
        // call original deserialize function
        auto *assetData = assetHandler->Deserialize(loadingContext, assetReader, (void*)ctx.r9);
        ctx.rax = (uintptr_t)assetData;
    });

    return true;
}

bool InstallHooks() {
    if (!InstallDeserializeAssetHook()) {
        _MESSAGE("Failed to install DeserializeAsset hook");
        return false;
    }
    if (!InstallGetArchiveInfoFromAssetLoaderHook()) {
        _MESSAGE("Failed to install GetArchiveInfoFromAssetLoader hook");
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

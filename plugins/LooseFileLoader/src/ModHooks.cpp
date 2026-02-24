#include "ModHooks.h"

#include "AssetRuntimeTypes.h"
#include "LooseModFileReader.h"
#include "ModAssetIndex.h"

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

using FnLoadAssetFromFile =
    bool (*)(std::uintptr_t, std::uintptr_t, void*, void*, void*, std::uintptr_t,
        GameAsset*, RdbRuntimeEntryDesc*, IAssetStreamReader*,
        std::uintptr_t, std::uintptr_t, void*, void*, bool);

using FnGetAssetHandlerFromType = IBaseGameAssetHandler* (*)(void*, std::uint32_t);

#ifdef _DEBUG
using FnRegisterAssetHandler = bool (*)(void*, std::uint32_t, IBaseGameAssetHandler*);
#endif

FnGetAssetHandlerFromType g_getAssetHandlerFromType = nullptr;

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
    FnLoadAssetFromFile original,
    std::uintptr_t param1, std::uintptr_t param2, void* assetManager,
    void* mountCallerCtx, void* mountCallbackCtx, std::uintptr_t param6,
    GameAsset* targetAsset, RdbRuntimeEntryDesc* rdbEntryDesc, IAssetStreamReader* assetStreamReader,
    std::uintptr_t payloadDataOffset, std::uintptr_t payloadSize, void* decompressor,
    void* handlerUserCtx, bool requireDecompress) {

    if (g_getAssetHandlerFromType == nullptr ||
        assetManager == nullptr || targetAsset == nullptr || rdbEntryDesc == nullptr) {
        return std::nullopt;
    }

    const auto fileId = rdbEntryDesc->fileKtid;
    if (fileId == 0xFFFFFFFF) {
        return std::nullopt;
    }

    const auto typeId = rdbEntryDesc->typeInfoKtid;
    IBaseGameAssetHandler* assetHandler = g_getAssetHandlerFromType(assetManager, typeId);
    const std::string typeName = assetHandler ? assetHandler->GetTypeName() : "Unknown";

    _MESSAGE("Loading asset: 0x%08X | Type: %s (0x%08X) | Size: %s", fileId, typeName.c_str(), typeId, FormatDiskSize(rdbEntryDesc->fileSize).c_str());

    const auto overridePath = g_modAssetIndex.Find(fileId);
    if (!overridePath.has_value()) {
        return std::nullopt;
    }

    std::error_code ec;
    if (!fs::exists(*overridePath, ec) || !fs::is_regular_file(*overridePath, ec)) {
        return std::nullopt;
    }

    auto reader = std::make_unique<LooseModFileReader>(*overridePath);
    if (!reader->IsOpen()) {
        _MESSAGE("Failed to open mod asset file: %s", overridePath->string().c_str());
        return std::nullopt;
    }


    const auto oldFlags = rdbEntryDesc->flags;
    rdbEntryDesc->flags = oldFlags | 0x100000;

    const bool result = original(
        param1, param2, assetManager, mountCallerCtx, mountCallbackCtx, param6,
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
void InstallRegisterAssetHandlerDebugHook() {
    auto registerAssetHandler = reinterpret_cast<FnRegisterAssetHandler>(
        HookUtils::ScanIDAPattern("E8 ? ? ? ? 84 C0 0F 84 ? ? ? ? 83 65 ? 00 48 8D 15", 0, 1, 5));
    _MESSAGE("RegisterAssetHandler: %p", registerAssetHandler);

    HookLambda(registerAssetHandler, [](void* assetManager, std::uint32_t assetHash,
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

        return original(assetManager, assetHash, assetHandler);
    });
}
#endif

}  // namespace

bool InstallLooseFileLoaderHooks() {
    auto loadAssetFromFile = reinterpret_cast<FnLoadAssetFromFile>(
        HookUtils::ScanIDAPattern("E8 ? ? ? ? 48 8B 8F ? ? ? ? 48 8B 53", 0, 1, 5));
    if (loadAssetFromFile == nullptr) {
        _MESSAGE("Failed to resolve LoadAssetFromFile");
        return false;
    }
    _MESSAGE("LoadAssetFromFile: %p", loadAssetFromFile);

    g_getAssetHandlerFromType = reinterpret_cast<FnGetAssetHandlerFromType>(
        HookUtils::ReadOffsetData(
            HookUtils::LookupFunctionPattern(
                loadAssetFromFile, "E8 ? ? ? ? 48 8B 4D ? 48 8B F8 4C 8B C6", 0x1000),
            1, 5));
    if (g_getAssetHandlerFromType == nullptr) {
        _MESSAGE("Failed to resolve GetAssetHandlerFromType");
        return false;
    }
    _MESSAGE("GetAssetHandlerFromType: %p", g_getAssetHandlerFromType);

    HookLambda(loadAssetFromFile, [](std::uintptr_t param1, std::uintptr_t param2, void* assetManager,
                                   void* mountCallerCtx, void* mountCallbackCtx, std::uintptr_t param6,
                                   GameAsset* targetAsset, RdbRuntimeEntryDesc* rdbEntryDesc,
                                   IAssetStreamReader* assetStreamReader, std::uintptr_t payloadDataOffset,
                                   std::uintptr_t payloadSize, void* decompressor, void* handlerUserCtx,
                                   bool requireDecompress) -> bool {
        if (const auto overrideResult = TryLoadAssetOverride(
                original, param1, param2, assetManager, mountCallerCtx, mountCallbackCtx, param6,
                targetAsset, rdbEntryDesc, assetStreamReader, payloadDataOffset, payloadSize,
                decompressor, handlerUserCtx, requireDecompress);
            overrideResult.has_value()) {
            return *overrideResult;
        }

        return original(param1, param2, assetManager, mountCallerCtx, mountCallbackCtx,
            param6, targetAsset, rdbEntryDesc, assetStreamReader,
            payloadDataOffset, payloadSize, decompressor, handlerUserCtx,
            requireDecompress);
    });

#ifdef _DEBUG
    InstallRegisterAssetHandlerDebugHook();
#endif
    return true;
}

}  // namespace LooseFileLoader

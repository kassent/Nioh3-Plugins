#define NOMINMAX
#include <Windows.h>
#include <cstdint>
#include <PluginAPI.h>
#include <LogUtils.h>
#include <FileUtils.h>
#include <HookUtils.h>
#include <BranchTrampoline.h>
#include <GameType.h>

#define PLUGIN_NAME "UnlimitedTransmog"
#define PLUGIN_VERSION_MAJOR 1
#define PLUGIN_VERSION_MINOR 4
#define PLUGIN_VERSION_PATCH 0

namespace {
    struct UserConfig {
        bool enableUnlockAllTransmog = true;
        bool enableSamuraiNinjaSharedTransmog = true;
    };

    std::filesystem::path GetIniPath(const Nioh3PluginInitializeParam* param) {
        std::filesystem::path pluginsDir = (param && param->plugins_dir) ? param->plugins_dir : "";
        std::string moduleName = PLUGIN_NAME;
        return pluginsDir / (moduleName + ".ini");
    }

    bool ReadIniBool(const std::filesystem::path& iniPath, const char* section, const char* key, bool defaultValue) {
        int value = GetPrivateProfileIntA(section, key, defaultValue ? 1 : 0, iniPath.string().c_str());
        return value != 0;
    }

    UserConfig LoadUserConfig(const Nioh3PluginInitializeParam* param) {
        UserConfig config{};
        auto iniPath = GetIniPath(param);
        _MESSAGE("Config ini path: %s", iniPath.string().c_str());

        if (!std::filesystem::exists(iniPath)) {
            _MESSAGE("Config file not found, using defaults.");
            return config;
        }

        config.enableUnlockAllTransmog = ReadIniBool(iniPath, PLUGIN_NAME, "EnableUnlockAllTransmog", true);
        config.enableSamuraiNinjaSharedTransmog = ReadIniBool(iniPath, PLUGIN_NAME, "EnableSamuraiNinjaSharedTransmog", true);

        _MESSAGE("Config loaded: EnableUnlockAllTransmog=%d, EnableSamuraiNinjaSharedTransmog=%d",
            config.enableUnlockAllTransmog ? 1 : 0,
            config.enableSamuraiNinjaSharedTransmog ? 1 : 0);

        return config;
    }


    // 语义：近战武器 group -> type
    // 对应 IDA: sub_142393D40
    int32_t GetMeleeWeaponDisplayType(int weaponGroup) {
        switch (weaponGroup) {
            case 6409:  return 0;
            case 24575: return 1;
            case 28275: return 2;
            case 21589: return 3;
            case 20629: return 4;
            case 7191:  return 5;
            case 11583: return 6;
            case 29361: return 7;
            case 24091: return 8;
            case 636:   return 9;
            case 3375:  return 10;
            case 6102:  return 11;
            case 1254:  return 12;
            case 9554:  return 13;
            default:    return -1;
        }
    }
    
    // 语义：远程武器 group -> type
    // 对应 IDA: sub_142393E0C
    int32_t GetRangedWeaponDisplayType(int rangedGroup) {
        switch (rangedGroup) {
            case 59886: return 14;
            case 49224: return 15;
            case 51013: return 16;
            default:    return -1;
        }
    }
    
    // 语义：防具 group -> type
    // 这是 ConvertSlotTypeToArmorType_1423939DC 的逆向映射
    int32_t GetArmorDisplayType(int armorGroup) {
        switch (armorGroup) {
            case 3577:  return 17; // head
            case 11055: return 18; // chest
            case 1975:  return 19; // arms
            case 16443: return 20; // knee/waist
            case 2473:  return 21; // legs
            default:    return -1;
        }
    }

    // 可选：统一入口（按 item category 取 type）
    int32_t GetItemDisplayType(const ItemData* item) {
        if (!item) return -1;
        switch (item->category) {
            case ITEM_CATEGORY_WEAPON: return GetMeleeWeaponDisplayType(static_cast<int>(item->weaponType));
            case ITEM_CATEGORY_GUN: return GetRangedWeaponDisplayType(static_cast<int>(item->gunType));
            case ITEM_CATEGORY_ARMOR: return GetArmorDisplayType(static_cast<int>(item->armorType));
            default: return -1;
        }
    }
}

extern "C" __declspec(dllexport) bool nioh3_plugin_initialize(const Nioh3PluginInitializeParam * param) {
    _MESSAGE("Plugin initialized");
    _MESSAGE("Game version: %s", param->game_version_string);
    _MESSAGE("Plugin dir: %s", param->plugins_dir);

    RELOC_MEMBER_FN(ItemDataManager, GetItemData, "E8 ? ? ? ? 45 33 C0 48 85 C0 74 ? 48 8B 87", 0, 1, 5);
    RELOC_GLOBAL_VAL(GetLocalizedString, "E8 ? ? ? ? 33 F6 48 C7 45 ? ? ? ? ? 48 8D 1D", 0, 1, 5);
    RELOC_GLOBAL_VAL(g_resManager, "48 8B 05 ? ? ? ? 41 8B D7 48 8B 98", 0, 3, 7);

    auto config = LoadUserConfig(param);

    if (config.enableUnlockAllTransmog) {
        auto patchAddr1 = HookUtils::ScanIDAPattern("E8 ? ? ? ? 84 C0 74 ? 45 8B C7 49 8B D3");
        if (patchAddr1) {
            _MESSAGE("Found addr for equipment unlock check: %p", patchAddr1);
            static auto isItemUnlockedMidHook = safetyhook::create_mid(patchAddr1 + 5, [](SafetyHookContext& ctx) {
                auto itemId = static_cast<uint16_t>(ctx.r13);
                auto *itemData = (*g_resManager)->itemData->GetItemData(itemId);
                ctx.rax = !itemData || itemData->GetName().empty() || GetItemDisplayType(itemData) == -1 ? 0 : 1;
            });
        } else {
            _MESSAGE("patchAddr1 not found.");
        }
    } else {
        _MESSAGE("Unlock-all-equipment disabled by config.");
    }

    if (config.enableSamuraiNinjaSharedTransmog) {
        uint8_t codes[] = {0xB0, 0x01, 0x90, 0x90, 0x90};
        auto patchAddr2 = HookUtils::ScanIDAPattern("E8 ? ? ? ? 84 C0 74 ? 45 0F B7 C5");
        if (patchAddr2) {
            _MESSAGE("Found addr for samurai-ninja shared-transmog check: %p", patchAddr2);
            HookUtils::SafeWriteBuf(patchAddr2, codes, sizeof(codes));
        } else {
            _MESSAGE("patchAddr2 not found.");
        }
    } else {
        _MESSAGE("Samurai-ninja shared-transmog disabled by config.");
    }

    return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        initLogger(PLUGIN_NAME);
        _MESSAGE("===============================================================");
        _MESSAGE("Initializing plugin: %s, version: %d.%d.%d", PLUGIN_NAME, PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR, PLUGIN_VERSION_PATCH);
        if (!g_branchTrampoline.Create(160)) {
            _MESSAGE("couldn't create branch trampoline. this is fatal. skipping remainder of init process.");
            return FALSE;
        }
        _MESSAGE("Branch trampoline created.");
    }
    return TRUE;
}

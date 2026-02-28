#define NOMINMAX
#include <Windows.h>

#include <PluginAPI.h>
#include <BranchTrampoline.h>
#include <LogUtils.h>

#include "Common.h"
#include "ModHooks.h"
#include "ModAssetManager.h"

namespace {
    std::filesystem::path GetIniPath(const Nioh3PluginInitializeParam* param) {
        std::filesystem::path pluginsDir = (param && param->plugins_dir) ? param->plugins_dir : "";
        std::string moduleName = PLUGIN_NAME;
        return pluginsDir / (moduleName + ".ini");
    }

    bool ReadIniBool(const std::filesystem::path& iniPath, const char* section, const char* key, bool defaultValue) {
        if (!std::filesystem::exists(iniPath)) {
            return defaultValue;
        }
        int value = GetPrivateProfileIntA(section, key, defaultValue ? 1 : 0, iniPath.string().c_str());
        return value != 0;
    }
}


using namespace LooseFileLoader;
// cmake --build build --config Release --target LooseFileLoader
extern "C" __declspec(dllexport) bool nioh3_plugin_initialize(const Nioh3PluginInitializeParam* param) {
    _MESSAGE("Plugin initialized");
    _MESSAGE("Game version: %s", param->game_version_string);
    _MESSAGE("Plugin dir: %s", param->plugins_dir);

    auto iniPath = GetIniPath(param);
    g_enableAssetLoadingLog = ReadIniBool(iniPath, PLUGIN_NAME, "EnableAssetLoadingLog", false);
    _MESSAGE("EnableAssetLoadingLog: %d", g_enableAssetLoadingLog ? 1 : 0);

    g_modAssetManager.Build(param->game_root_dir);
    if (!InstallHooks()) {
        _MESSAGE("Failed to install LooseFileLoader hooks");
        return false;
    }

    return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        _MESSAGE("Initializing plugin: %s, version: %d.%d.%d",
            PLUGIN_NAME, PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR, PLUGIN_VERSION_PATCH);
    }
    return TRUE;
}


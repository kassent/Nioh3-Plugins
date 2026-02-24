#define NOMINMAX
#include <Windows.h>

#include <PluginAPI.h>
#include <BranchTrampoline.h>
#include <LogUtils.h>

#include "ModHooks.h"
#include "ModAssetIndex.h"

#define PLUGIN_NAME "LooseFileLoader"
#define PLUGIN_VERSION_MAJOR 1
#define PLUGIN_VERSION_MINOR 0
#define PLUGIN_VERSION_PATCH 0

using namespace LooseFileLoader;
// cmake --build build --config Release --target LooseFileLoader
extern "C" __declspec(dllexport) bool nioh3_plugin_initialize(const Nioh3PluginInitializeParam* param) {
    _MESSAGE("Plugin initialized");
    _MESSAGE("Game version: %s", param->game_version_string);
    _MESSAGE("Plugin dir: %s", param->plugins_dir);

    g_modAssetIndex.Build(param->game_root_dir);
    if (!InstallLooseFileLoaderHooks()) {
        _MESSAGE("Failed to install LooseFileLoader hooks");
        return false;
    }

    return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        initLogger(PLUGIN_NAME);
        _MESSAGE("===============================================================");
        _MESSAGE("Initializing plugin: %s, version: %d.%d.%d",
            PLUGIN_NAME, PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR, PLUGIN_VERSION_PATCH);

        if (!g_branchTrampoline.Create(160)) {
            _MESSAGE("couldn't create branch trampoline. this is fatal. skipping remainder of init process.");
            return FALSE;
        }
        _MESSAGE("Branch trampoline created.");
    }
    return TRUE;
}


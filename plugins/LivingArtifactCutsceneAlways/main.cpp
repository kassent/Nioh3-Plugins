#define NOMINMAX
#include <Windows.h>
#include <PluginAPI.h>
#include <LogUtils.h>
#include <FileUtils.h>
#include <HookUtils.h>
#include <BranchTrampoline.h>
#include <GameType.h>

#define PLUGIN_NAME "LivingArtifactCutsceneAlways"
#define PLUGIN_VERSION_MAJOR 1
#define PLUGIN_VERSION_MINOR 0
#define PLUGIN_VERSION_PATCH 0

extern "C" __declspec(dllexport) bool nioh3_plugin_initialize(const Nioh3PluginInitializeParam * param) {
    _MESSAGE("Plugin initialized");
    _MESSAGE("Game version: %s", param->game_version_string);
    _MESSAGE("Plugin dir: %s", param->plugins_dir);

    auto addr = HookUtils::ScanIDAPattern("85 D2 74 ? 48 8B C4 48 89 58 ? 89 50 ? 57");
    if (addr) {
        _MESSAGE("Found patch addr: %p", addr);
        static auto livingArtifactCutsceneAlwaysAnimateMidHook = safetyhook::create_mid(addr, [](SafetyHookContext& ctx) {
            ctx.r8 = 0;
        });
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

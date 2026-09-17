# Nioh3 Plugins

**English** | [简体中文](README.zh-CN.md)

A collection of native Windows x64 plugins for **Nioh 3**, built with C++23, CMake, and MSVC. Each plugin can be used independently. Shared code provides address resolution, hooks, logging, configuration helpers, and game data structures.

## Plugins

| Plugin | Features | Current status |
| --- | --- | --- |
| [LooseFileLoader](plugins/LooseFileLoader) | Load replacement models, textures, and other assets from disk without repacking game archives | `1.3.0`; updated for game version `2.0.2.0` and built in Release mode |
| [UnlimitedTransmog](plugins/UnlimitedTransmog) | Unlock equipment transmogs and relax Samurai/Ninja transmog restrictions | `1.0.4`; updated for game version `2.0.2.0` and built in Release mode |
| [LivingArtifactCutsceneAlways](plugins/LivingArtifactCutsceneAlways) | Adjust Living Artifact transformation cutscene conditions so the animation always plays | Legacy implementation; not included in the latest compatibility or build checks |
| [GameDumper](plugins/GameDumper) | Dump equipment data and investigate model/resource loading | Research code; not included in the latest compatibility or build checks |

Game updates can change function addresses and data layouts. The status above does not imply that every plugin or shared structure in this repository supports that game version. Static checks are complete for the two updated plugins; in-game behavior still needs testing.

## Installation

This repository contains plugin sources and shared libraries. A compatible plugin loader is required separately. It must support the interface in [PluginAPI.h](common/include/PluginAPI.h) and call each plugin's exported `nioh3_plugin_initialize` function.

1. Close the game and copy the desired plugin DLLs into the game's `plugins` folder.
2. Copy the matching INI templates into the same folder. Preserve your configuration when upgrading.
3. For LooseFileLoader, place replacement assets in the game's `mods` folder.
4. Start the game and check the plugin initialization messages in the `logs` folder.

```text
Nioh3/
├── Nioh3.exe
├── plugins/
│   ├── LooseFileLoader.dll
│   ├── LooseFileLoader.ini
│   ├── UnlimitedTransmog.dll
│   └── UnlimitedTransmog.ini
├── mods/
│   ├── 0x1234ABCD.g1m
│   └── MyTextureMod/
│       └── 0x89ABCDEF.g1t
└── logs/
    ├── LooseFileLoader.log
    └── UnlimitedTransmog.log
```

To uninstall a plugin, close the game and remove its DLL. Configuration files and replacement assets can be kept if desired.

## Usage and Configuration

### LooseFileLoader

Configuration template: [LooseFileLoader.ini](plugins/LooseFileLoader/LooseFileLoader.ini).

```ini
[LooseFileLoader]
EnableAssetLoadingLog=0
DisableStreamingLoading=0
EnableConsole=0
AssetLoggerFilters=ModelData,StreamingTexContext,TexContext
```

| Setting | Description |
| --- | --- |
| `EnableAssetLoadingLog` | Set to `1` to log asset loads and help identify resources to replace |
| `DisableStreamingLoading` | Set to `1` to disable streaming texture loading for debugging; normally keep it at `0` |
| `EnableConsole` | Set to `1` to display a logging console |
| `AssetLoggerFilters` | Comma-separated, case-sensitive asset types to log; leave empty to log all types |

Asset placement rules:

- The filename stem must be an **8-digit hexadecimal resource ID**, optionally prefixed with `0x`, such as `1234ABCD.g1m` or `0x1234ABCD.g1m`.
- Only the `mods` root and its **immediate subdirectories** are scanned. Deeper folders are not scanned recursively.
- If several files replace the same resource ID, files in the root take priority. Subdirectories are sorted by name, and the first match wins. Conflicts are logged.
- File extensions are ignored when parsing IDs, but file contents must match the target asset format.
- The override index is built at startup. Restart the game after adding or removing replacement files.

### UnlimitedTransmog

Configuration template: [UnlimitedTransmog.ini](plugins/UnlimitedTransmog/UnlimitedTransmog.ini).

```ini
[UnlimitedTransmog]
EnableUnlockAllTransmog=1
EnableSamuraiNinjaSharedTransmog=1
```

- `EnableUnlockAllTransmog`: unlock transmog options for valid equipment.
- `EnableSamuraiNinjaSharedTransmog`: relax Samurai/Ninja transmog restrictions. This does not make arbitrary weapon types interchangeable.

The options are independent: `1` enables an option and `0` disables it. Both default to enabled if the configuration is missing. Restart the game after making changes.

## Building from Source

### Requirements

- Windows x64.
- Visual Studio 2022 / MSVC with the Desktop development with C++ workload and Windows SDK. The toolchain must support the C++23 features used by the project.
- CMake **3.21 or newer**.
- vcpkg with the `x64-windows-static` triplet.

[vcpkg.json](vcpkg.json) declares the `spdlog`, `fmt`, `zlib`, and `nlohmann-json` dependencies. Sources for SafetyHook, LightningScanner, and other shared components are included under `common`.

### Check Local Paths First

The root [CMakeLists.txt](CMakeLists.txt) currently contains machine-specific absolute paths. Update them for your environment before the first build:

| Setting | Current path | Notes |
| --- | --- | --- |
| vcpkg toolchain | `D:/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake` | Check both `CMAKE_TOOLCHAIN_FILE` and the explicit `include(...)`; a command-line toolchain override does not replace the latter |
| Post-build copy directory | `E:/SteamLibrary/steamapps/common/Nioh3/plugins` | Plugin DLLs are copied here after building; create the directory beforehand |
| Visual Studio debug executable | `H:/SteamLibrary/steamapps/common/Nioh3/Nioh3.exe` | Update to your game's executable path as needed |

**Building a plugin also deploys it.** Adjust the `POST_BUILD` copy step first if you only want to compile without replacing the installed DLL. A running game may lock the DLL and cause the copy to fail; check build and deployment results separately.

### Configure and Build Individual Targets

Run these PowerShell commands from the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

cmake --build build --config Release --target LooseFileLoader
cmake --build build --config Release --target UnlimitedTransmog
```

DLLs are written to `build/bin/Release/`, and shared static libraries to `build/lib/Release/`. The build scripts only copy DLLs automatically; install INI templates manually.

Specify the desired `--target` instead of building everything, including research or outdated plugins. If a `build` directory already exists, use its configured generator and architecture.

## Development and Validation

```text
common/                         Shared utilities and game type definitions
plugins/LooseFileLoader/         Asset overrides, RDB tools, and tests
plugins/UnlimitedTransmog/       Transmog plugin and compatibility checks
plugins/LivingArtifactCutsceneAlways/
plugins/GameDumper/              Data dumps and resource-loading research
```

### UnlimitedTransmog Offline Checks

The script requires Python, `pefile`, `capstone`, and an unpacked game executable for version `2.0.2.0` that you provide:

```powershell
python -m pip install pefile capstone
python plugins/UnlimitedTransmog/analysis/verify_2_0_2.py "E:/SteamLibrary/steamapps/common/Nioh3/Nioh3.exe.unpacked.exe"
```

It checks signature matches, call targets, field offsets, register usage, and equipment type mappings. The script only reads files: it does not run or modify the game, and it does not replace in-game testing.

### LooseFileLoader RDB Tool Tests

```powershell
cmake --build build --config Release --target LooseFileLoaderRdbToolTests
./build/bin/Release/LooseFileLoaderRdbToolTests.exe
```

Provide RDB test fixtures under `plugins/LooseFileLoader/package`; these fixtures are not included in the Git repository. Tests copy them into a temporary workspace before running. See the [LooseFileLoader README](plugins/LooseFileLoader/README.md) for details.

### Compatibility Notes

The detailed analysis documents below are in Chinese:

- [LooseFileLoader runtime field checks](plugins/LooseFileLoader/analysis/Common_runtime_fields_2.0.2.0.md)
- [GameManager offsets and signatures](plugins/LooseFileLoader/analysis/GameManager_offsets_2.0.2.0.md)
- [Additional LooseFileLoader signature target checks](plugins/LooseFileLoader/analysis/Other_pattern_targets_2.0.2.0.md)
- [UnlimitedTransmog 2.0.2.0 compatibility and validation](plugins/UnlimitedTransmog/analysis/Compatibility_2.0.2.0.md)

## Troubleshooting

- **A plugin has no effect:** confirm that the loader loaded its DLL, then check version, initialization, and signature messages in `logs/<PluginName>.log` under the game directory.
- **`Pattern not found`:** compatibility usually needs to be checked against the current game version. A byte-pattern match alone also does not prove that the resolved function is correct.
- **An asset override has no effect:** check the filename ID, directory depth, and conflict messages. Restart after adding new files.
- **Configuration is not found:** place the INI beside the plugin DLL and check its filename and section name.
- **The build succeeds but deployment fails:** check the copy destination, directory permissions, and whether the game has the DLL open.

Shared logging writes to the game's `logs` folder and overwrites the corresponding log on startup. Save the previous log before restarting if you need it for troubleshooting.

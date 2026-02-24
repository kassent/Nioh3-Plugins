# LooseFileLoader

## 1. What Is Included

This plugin currently contains:

- Runtime loose-file loading logic (`LooseFileLoader.dll`)
- `RdbTool` resource helper (`dump / extract / replace / insert`)
- `RdbTool` test executable (`LooseFileLoaderRdbToolTests.exe`)

## 2. Prerequisites

Run commands from repository root: `d:/dev/nioh3`.

Required tools:

- Visual Studio 2022 (MSVC)
- CMake (>= 3.21)
- vcpkg

Note:

- Root `CMakeLists.txt` currently uses hardcoded vcpkg path `D:/vcpkg/vcpkg`.
- If your vcpkg location is different, update `CMAKE_TOOLCHAIN_FILE` and related `include(...)` lines in root `CMakeLists.txt`.

## 3. Configure

```powershell
cmake -S . -B build
```

## 4. Build Plugin

```powershell
cmake --build build --config Release --target LooseFileLoader
```

Output:

- `build/bin/Release/LooseFileLoader.dll`

Note:

- Current root `CMakeLists.txt` has a post-build copy step to:
- `E:/SteamLibrary/steamapps/common/Nioh3/plugins`
- If your game path is different, update the `POST_BUILD` copy command.

## 5. Build Tests

```powershell
cmake --build build --config Release --target LooseFileLoaderRdbToolTests
```

Output:

- `build/bin/Release/LooseFileLoaderRdbToolTests.exe`

## 6. Run Tests

```powershell
./build/bin/Release/LooseFileLoaderRdbToolTests.exe
```

Example success output:

```text
[PASS] RdbTool tests passed.
Test workspace: C:\Users\<User>\AppData\Local\Temp\LooseFileLoader_RdbToolTest_<pid>
```

Test flow:

- Copy `plugins/LooseFileLoader/package` to temp workspace
- Parse and dump `root.rdb/root.rdx`
- Run `extract`
- Run `replace` and validate payload
- Run `insert(reuse=true)` and validate
- Run `insert(custom typeInfoKtid)` and validate

The tests do not modify original files under `plugins/LooseFileLoader/package`.

## 7. Quick Commands

```powershell
# Configure
cmake -S . -B build

# Build plugin
cmake --build build --config Release --target LooseFileLoader

# Build tests
cmake --build build --config Release --target LooseFileLoaderRdbToolTests

# Run tests
./build/bin/Release/LooseFileLoaderRdbToolTests.exe
```

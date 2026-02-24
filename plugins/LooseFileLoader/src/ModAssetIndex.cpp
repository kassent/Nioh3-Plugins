#define NOMINMAX
#include "ModAssetIndex.h"

#include <Windows.h>

#include <LogUtils.h>

#include <algorithm>
#include <cwctype>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace LooseFileLoader {
namespace {

struct ModAssetCandidate {
    std::uint32_t fileHash = 0;
    fs::path filePath{};
    bool fromModsRoot = false;
    std::wstring parentSortKey{};
    std::wstring fileSortKey{};
};

int CompareWideOrdinal(const std::wstring& lhs, const std::wstring& rhs, bool ignoreCase) {
    const int result = CompareStringOrdinal(lhs.c_str(), -1, rhs.c_str(), -1, ignoreCase ? TRUE : FALSE);
    if (result == CSTR_LESS_THAN) {
        return -1;
    }
    if (result == CSTR_GREATER_THAN) {
        return 1;
    }
    if (result == CSTR_EQUAL) {
        return 0;
    }

    if (lhs < rhs) {
        return -1;
    }
    if (lhs > rhs) {
        return 1;
    }
    return 0;
}

bool LessWideNoCaseStable(const std::wstring& lhs, const std::wstring& rhs) {
    const int ci = CompareWideOrdinal(lhs, rhs, true);
    if (ci != 0) {
        return ci < 0;
    }
    return CompareWideOrdinal(lhs, rhs, false) < 0;
}

bool TryParseAssetHashFromFileName(const fs::path& path, std::uint32_t& outHash) {
    std::wstring fileName = path.stem().wstring();
    if (fileName.empty()) {
        return false;
    }

    std::wstring hexText;
    if (fileName.size() == 10 && fileName[0] == L'0' && (fileName[1] == L'x' || fileName[1] == L'X')) {
        hexText = fileName.substr(2);
    } else if (fileName.size() == 8) {
        hexText = fileName;
    } else {
        return false;
    }

    if (!std::all_of(hexText.begin(), hexText.end(), [](wchar_t ch) { return std::iswxdigit(ch) != 0; })) {
        return false;
    }

    try {
        const auto value = std::stoull(hexText, nullptr, 16);
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        outHash = static_cast<std::uint32_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

void CollectModAssetCandidates(const fs::path& dir, bool fromModsRoot, const std::wstring& parentSortKey,
    std::vector<ModAssetCandidate>& outCandidates) {
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        return;
    }

    for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            _MESSAGE("Failed to iterate directory: %s", dir.string().c_str());
            break;
        }

        std::error_code fileEc;
        if (!entry.is_regular_file(fileEc)) {
            continue;
        }

        std::uint32_t fileHash = 0;
        if (!TryParseAssetHashFromFileName(entry.path(), fileHash)) {
            continue;
        }

        ModAssetCandidate candidate{};
        candidate.fileHash = fileHash;
        candidate.filePath = entry.path();
        candidate.fromModsRoot = fromModsRoot;
        candidate.parentSortKey = parentSortKey;
        candidate.fileSortKey = entry.path().filename().wstring();
        outCandidates.emplace_back(std::move(candidate));
    }
}

}  // namespace

void ModAssetIndex::Build(const fs::path& gameRootDir) {
    overrides_.clear();

    const fs::path modsDir = gameRootDir / "mods";
    std::error_code ec;
    if (!fs::exists(modsDir, ec) || !fs::is_directory(modsDir, ec)) {
        _MESSAGE("Mods directory not found: %s", modsDir.string().c_str());
        return;
    }

    std::vector<ModAssetCandidate> candidates;
    CollectModAssetCandidates(modsDir, true, L"", candidates);

    std::vector<std::pair<std::wstring, fs::path>> firstLevelModDirs;
    for (const auto& entry : fs::directory_iterator(modsDir, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            _MESSAGE("Failed to iterate mods root: %s", modsDir.string().c_str());
            break;
        }

        std::error_code dirEc;
        if (!entry.is_directory(dirEc)) {
            continue;
        }

        firstLevelModDirs.emplace_back(entry.path().filename().wstring(), entry.path());
    }

    std::sort(firstLevelModDirs.begin(), firstLevelModDirs.end(), [](const auto& lhs, const auto& rhs) {
        const int folderNameCmp = CompareWideOrdinal(lhs.first, rhs.first, true);
        if (folderNameCmp != 0) {
            return folderNameCmp < 0;
        }
        return LessWideNoCaseStable(lhs.second.filename().wstring(), rhs.second.filename().wstring());
    });

    for (const auto& [folderSortKey, folderPath] : firstLevelModDirs) {
        CollectModAssetCandidates(folderPath, false, folderSortKey, candidates);
    }

    std::sort(candidates.begin(), candidates.end(), [](const ModAssetCandidate& lhs, const ModAssetCandidate& rhs) {
        if (lhs.fromModsRoot != rhs.fromModsRoot) {
            return lhs.fromModsRoot && !rhs.fromModsRoot;
        }
        if (!lhs.fromModsRoot) {
            const int parentCmp = CompareWideOrdinal(lhs.parentSortKey, rhs.parentSortKey, true);
            if (parentCmp != 0) {
                return parentCmp < 0;
            }
        }
        const int fileCmp = CompareWideOrdinal(lhs.fileSortKey, rhs.fileSortKey, true);
        if (fileCmp != 0) {
            return fileCmp < 0;
        }
        return LessWideNoCaseStable(lhs.filePath.wstring(), rhs.filePath.wstring());
    });

    std::size_t conflictCount = 0;
    for (const auto& candidate : candidates) {
        const auto [it, inserted] = overrides_.emplace(candidate.fileHash, candidate.filePath);
        if (!inserted) {
            ++conflictCount;
            _MESSAGE("Mod override conflict for 0x%08X: keep=%s, skip=%s",
                candidate.fileHash, it->second.string().c_str(), candidate.filePath.string().c_str());
        }
    }

    _MESSAGE("Mod override index built. candidates=%zu, unique=%zu, conflicts=%zu",
        candidates.size(), overrides_.size(), conflictCount);
}

std::optional<fs::path> ModAssetIndex::Find(std::uint32_t fileHash) const {
    const auto it = overrides_.find(fileHash);
    return (it != overrides_.end()) ? std::optional<fs::path>(it->second) : std::nullopt;
}

}  // namespace LooseFileLoader


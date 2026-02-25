#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <unordered_map>

namespace LooseFileLoader {

class ModAssetManager {
public:
    void Build(const std::filesystem::path& gameRootDir);
    [[nodiscard]] std::optional<std::filesystem::path> Find(std::uint32_t fileHash) const;

private:
    std::unordered_map<std::uint32_t, std::filesystem::path> overrides_{};
};

inline ModAssetManager g_modAssetManager;
}  // namespace LooseFileLoader


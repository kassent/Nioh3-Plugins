#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace LooseFileLoader {

enum class RdbLocationFlags : std::uint16_t {
    Internal = 0x401,
    External = 0xC01,
};

struct RdxEntry {
    std::uint16_t index = 0;
    std::uint16_t marker = 0;
    std::uint32_t fileId = 0;
};

struct RdbHeader {
    std::array<char, 4> magic{'_', 'D', 'R', 'K'};
    std::uint32_t version = 0;
    std::uint32_t headerSize = 0;
    std::uint32_t systemId = 0;
    std::uint32_t fileCount = 0;
    std::uint32_t databaseId = 0;
    std::array<char, 8> folderPathRaw{};

    [[nodiscard]] std::string FolderPath() const;
};

struct RdbLocation {
    std::uint16_t newFlags = 0;
    std::uint64_t offset = 0;
    std::uint32_t sizeInContainer = 0;
    std::uint16_t fdataId = 0;
    bool uses64BitOffset = false;
    std::filesystem::path containerPath{};
};

struct RdbEntry {
    std::size_t index = 0;
    std::uint64_t entryOffsetInRdb = 0;
    std::array<char, 4> magic{'I', 'D', 'R', 'K'};
    std::uint32_t version = 0;
    std::uint64_t entrySize = 0;
    std::uint64_t dataSize = 0;
    std::uint64_t fileSize = 0;
    std::uint32_t entryType = 0;
    std::uint32_t fileKtid = 0;
    std::uint32_t typeInfoKtid = 0;
    std::uint32_t flags = 0;
    std::vector<std::byte> paramBlock{};
    std::vector<std::byte> metadataBlock{};
    bool hasLocation = false;
    RdbLocation location{};
};

class RdbTool final {
public:
    static std::optional<RdbTool> Open(const std::filesystem::path& rootRdbPath,
                                       const std::filesystem::path& rootRdxPath,
                                       std::string* error = nullptr);

    bool Reload(std::string* error = nullptr);

    [[nodiscard]] const RdbHeader& Header() const;
    [[nodiscard]] const std::vector<RdbEntry>& Entries() const;
    [[nodiscard]] const RdbEntry* FindEntryByFileKtid(std::uint32_t fileKtid) const;

    bool Dump(const std::filesystem::path& outputPath, std::string* error = nullptr) const;
    bool Extract(std::uint32_t fileKtid, const std::filesystem::path& outputPath, std::string* error = nullptr) const;

    bool Replace(std::uint32_t fileKtid, std::span<const std::byte> replacementData, std::string* error = nullptr);
    bool Replace(std::uint32_t fileKtid, const std::filesystem::path& inputFilePath, std::string* error = nullptr);

    bool Insert(std::uint32_t newFileKtid, std::uint32_t templateFileKtid,
                std::span<const std::byte> replacementData,
                std::uint32_t typeInfoKtid = 0,
                bool reuseTemplateData = false,
                std::string* error = nullptr);
    bool Insert(std::uint32_t newFileKtid, std::uint32_t templateFileKtid,
                const std::filesystem::path& inputFilePath,
                std::uint32_t typeInfoKtid = 0,
                bool reuseTemplateData = false,
                std::string* error = nullptr);
    bool Insert(std::uint32_t newFileKtid, std::uint32_t templateFileKtid,
                bool reuseTemplateData,
                std::uint32_t typeInfoKtid = 0,
                std::string* error = nullptr);

private:
    struct KrdiHeader {
        std::array<char, 4> magic{'I', 'D', 'R', 'K'};
        std::array<char, 4> version{'0', '0', '0', '0'};
        std::uint64_t allBlockSize = 0;
        std::uint64_t compressedSize = 0;
        std::uint64_t uncompressedSize = 0;
        std::uint32_t paramDataSize = 0;
        std::uint32_t hashName = 0;
        std::uint32_t hashType = 0;
        std::uint32_t flags = 0;
        std::uint32_t resourceId = 0;
        std::int32_t paramCount = 0;
    };

    struct ParsedKrdi {
        KrdiHeader header{};
        std::vector<std::byte> paramSection{};
        std::uint64_t payloadOffset = 0;
    };

    RdbTool() = default;

    bool ReadRdx(std::string* error);
    bool ReadRdb(std::string* error);
    bool ResolveContainerPath(RdbEntry* entry) const;
    bool ParseEntryLocation(RdbEntry* entry) const;

    bool ReadContainer(const RdbEntry& entry, std::filesystem::path* resolvedPath,
                       std::vector<std::byte>* outBytes, std::string* error) const;
    bool ParseKrdiAt(const std::vector<std::byte>& containerBytes, std::uint64_t offset,
                     ParsedKrdi* outKrdi, std::string* error) const;
    bool ExtractPayload(const std::vector<std::byte>& containerBytes,
                        const ParsedKrdi& krdi, std::vector<std::byte>* outPayload,
                        std::string* error) const;
    bool BuildModifiedKrdi(const ParsedKrdi& source, std::span<const std::byte> replacementData,
                           std::vector<std::byte>* outBlock, std::string* error) const;

    bool PatchEntryLocation(RdbEntry* entry, std::uint64_t newOffset, std::uint32_t newSize, std::string* error) const;
    bool SaveRdb(std::string* error);

    static bool ReadWholeFile(const std::filesystem::path& path, std::vector<std::byte>* outBytes, std::string* error);
    static bool WriteWholeFile(const std::filesystem::path& path, std::span<const std::byte> bytes, std::string* error);

    std::filesystem::path packageDir_{};
    std::filesystem::path rootRdbPath_{};
    std::filesystem::path rootRdxPath_{};
    RdbHeader header_{};
    std::vector<RdxEntry> rdxEntries_{};
    std::vector<RdbEntry> entries_{};
};

}  // namespace LooseFileLoader

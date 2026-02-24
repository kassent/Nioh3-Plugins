#include "RdbTool.h"

#include "binary_io/binary_io.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;

namespace LooseFileLoader {
namespace {

constexpr std::uint16_t kLocationInternal = static_cast<std::uint16_t>(RdbLocationFlags::Internal);
constexpr std::uint16_t kLocationExternal = static_cast<std::uint16_t>(RdbLocationFlags::External);
constexpr std::uint32_t kCompressionMask = (0x3Fu << 20);
constexpr std::uint32_t kCompressionZlib = 1;
constexpr std::uint32_t kCompressionEncrypted = 3;
constexpr std::uint32_t kCompressionExtended = 4;
constexpr std::size_t kRdbEntryHeaderSize = 48;
constexpr std::size_t kKrdiHeaderSize = 56;
constexpr std::size_t kDefaultChunkSize = 0x4000;

[[nodiscard]] std::uint64_t AlignUp(std::uint64_t value, std::uint64_t alignment) {
    if (alignment == 0) {
        return value;
    }
    const std::uint64_t remainder = value % alignment;
    return (remainder == 0) ? value : (value + (alignment - remainder));
}

[[nodiscard]] std::string Hex8(std::uint32_t value) {
    std::ostringstream oss;
    oss << std::hex << std::nouppercase << std::setfill('0') << std::setw(8) << value;
    return oss.str();
}

[[nodiscard]] bool ToSizeT(std::uint64_t value, std::size_t* out) {
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    *out = static_cast<std::size_t>(value);
    return true;
}

void SetError(std::string* error, std::string_view message) {
    if (error != nullptr) {
        *error = std::string(message);
    }
}

void WriteU16LE(std::byte* dst, std::uint16_t value) {
    dst[0] = static_cast<std::byte>(value & 0xFFu);
    dst[1] = static_cast<std::byte>((value >> 8) & 0xFFu);
}

void WriteU32LE(std::byte* dst, std::uint32_t value) {
    dst[0] = static_cast<std::byte>(value & 0xFFu);
    dst[1] = static_cast<std::byte>((value >> 8) & 0xFFu);
    dst[2] = static_cast<std::byte>((value >> 16) & 0xFFu);
    dst[3] = static_cast<std::byte>((value >> 24) & 0xFFu);
}

[[nodiscard]] bool ReadBytes(binary_io::span_istream& stream, std::span<std::byte> out, std::string* error) {
    try {
        stream.read_bytes(out);
        return true;
    } catch (const binary_io::buffer_exhausted&) {
        SetError(error, "Unexpected end of buffer while reading bytes.");
        return false;
    }
}

template <class... Args>
[[nodiscard]] bool ReadValues(binary_io::span_istream& stream, std::string* error, Args&... args) {
    try {
        stream.read(args...);
        return true;
    } catch (const binary_io::buffer_exhausted&) {
        SetError(error, "Unexpected end of buffer while reading values.");
        return false;
    }
}

[[nodiscard]] bool InflateZlibChunk(std::span<const std::byte> compressed,
                                    std::size_t expectedSize,
                                    std::vector<std::byte>* out,
                                    std::string* error) {
    out->clear();
    if (expectedSize == 0) {
        return true;
    }
    if (compressed.empty()) {
        SetError(error, "Compressed chunk is empty.");
        return false;
    }

    uLongf destLen = static_cast<uLongf>(expectedSize);
    out->resize(expectedSize);
    int result = ::uncompress(
        reinterpret_cast<Bytef*>(out->data()),
        &destLen,
        reinterpret_cast<const Bytef*>(compressed.data()),
        static_cast<uLong>(compressed.size()));

    if (result == Z_BUF_ERROR) {
        const std::size_t fallbackSize = std::max<std::size_t>(expectedSize * 4, expectedSize + 1024);
        out->resize(fallbackSize);
        destLen = static_cast<uLongf>(fallbackSize);
        result = ::uncompress(
            reinterpret_cast<Bytef*>(out->data()),
            &destLen,
            reinterpret_cast<const Bytef*>(compressed.data()),
            static_cast<uLong>(compressed.size()));
    }

    if (result != Z_OK) {
        SetError(error, "zlib chunk decompression failed.");
        return false;
    }

    out->resize(static_cast<std::size_t>(destLen));
    return true;
}

}  // namespace

std::string RdbHeader::FolderPath() const {
    std::string path(folderPathRaw.begin(), folderPathRaw.end());
    while (!path.empty() && path.back() == '\0') {
        path.pop_back();
    }
    return path;
}

std::optional<RdbTool> RdbTool::Open(const fs::path& rootRdbPath,
                                     const fs::path& rootRdxPath,
                                     std::string* error) {
    RdbTool tool;
    tool.rootRdbPath_ = rootRdbPath;
    tool.rootRdxPath_ = rootRdxPath;
    tool.packageDir_ = rootRdbPath.parent_path();
    if (!tool.Reload(error)) {
        return std::nullopt;
    }
    return tool;
}

bool RdbTool::Reload(std::string* error) {
    entries_.clear();
    rdxEntries_.clear();
    if (!ReadRdx(error)) {
        return false;
    }
    if (!ReadRdb(error)) {
        return false;
    }
    return true;
}

const RdbHeader& RdbTool::Header() const {
    return header_;
}

const std::vector<RdbEntry>& RdbTool::Entries() const {
    return entries_;
}

const RdbEntry* RdbTool::FindEntryByFileKtid(std::uint32_t fileKtid) const {
    const auto it = std::find_if(entries_.begin(), entries_.end(), [fileKtid](const RdbEntry& entry) {
        return entry.fileKtid == fileKtid;
    });
    if (it == entries_.end()) {
        return nullptr;
    }
    return &(*it);
}

bool RdbTool::Dump(const fs::path& outputPath, std::string* error) const {
    std::error_code ec;
    if (!outputPath.parent_path().empty()) {
        fs::create_directories(outputPath.parent_path(), ec);
    }

    std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        SetError(error, "Failed to open dump output file.");
        return false;
    }

    out << "RDB Header\n";
    out << "magic=" << std::string(header_.magic.data(), header_.magic.size()) << "\n";
    out << "version=0x" << Hex8(header_.version) << "\n";
    out << "headerSize=" << header_.headerSize << "\n";
    out << "systemId=" << header_.systemId << "\n";
    out << "fileCount=" << entries_.size() << "\n";
    out << "databaseId=0x" << Hex8(header_.databaseId) << "\n";
    out << "folderPath=" << header_.FolderPath() << "\n\n";

    out << "index,fileKtid,typeInfoKtid,fileSize,flags,dataSize,newFlags,offset,sizeInContainer,fdataId,container\n";
    for (const RdbEntry& entry : entries_) {
        out << entry.index << ",0x" << Hex8(entry.fileKtid)
            << ",0x" << Hex8(entry.typeInfoKtid)
            << "," << entry.fileSize
            << ",0x" << Hex8(entry.flags)
            << "," << entry.dataSize
            << ",";

        if (entry.hasLocation) {
            out << "0x" << std::hex << std::nouppercase << entry.location.newFlags << std::dec
                << "," << entry.location.offset
                << "," << entry.location.sizeInContainer
                << "," << entry.location.fdataId
                << "," << entry.location.containerPath.generic_string();
        } else {
            out << "n/a,n/a,n/a,n/a,n/a";
        }
        out << "\n";
    }

    return true;
}

bool RdbTool::Extract(std::uint32_t fileKtid, const fs::path& outputPath, std::string* error) const {
    const RdbEntry* entry = FindEntryByFileKtid(fileKtid);
    if (entry == nullptr) {
        SetError(error, "Entry not found for fileKtid.");
        return false;
    }
    if (!entry->hasLocation) {
        SetError(error, "Entry does not provide location metadata.");
        return false;
    }

    fs::path containerPath;
    std::vector<std::byte> containerBytes;
    if (!ReadContainer(*entry, &containerPath, &containerBytes, error)) {
        return false;
    }

    const std::uint64_t blockOffset = (entry->location.newFlags == kLocationInternal) ? entry->location.offset : 0;
    ParsedKrdi krdi;
    if (!ParseKrdiAt(containerBytes, blockOffset, &krdi, error)) {
        return false;
    }

    std::vector<std::byte> payload;
    if (!ExtractPayload(containerBytes, krdi, &payload, error)) {
        return false;
    }

    return WriteWholeFile(outputPath, payload, error);
}

bool RdbTool::Replace(std::uint32_t fileKtid, const fs::path& inputFilePath, std::string* error) {
    std::vector<std::byte> replacementData;
    if (!ReadWholeFile(inputFilePath, &replacementData, error)) {
        return false;
    }
    return Replace(fileKtid, replacementData, error);
}

bool RdbTool::Replace(std::uint32_t fileKtid, std::span<const std::byte> replacementData, std::string* error) {
    auto entryIt = std::find_if(entries_.begin(), entries_.end(), [fileKtid](const RdbEntry& entry) {
        return entry.fileKtid == fileKtid;
    });
    if (entryIt == entries_.end()) {
        SetError(error, "Entry not found for replace.");
        return false;
    }
    if (!entryIt->hasLocation) {
        SetError(error, "Target entry has no location metadata.");
        return false;
    }

    fs::path containerPath;
    std::vector<std::byte> containerBytes;
    if (!ReadContainer(*entryIt, &containerPath, &containerBytes, error)) {
        return false;
    }

    const bool isInternal = (entryIt->location.newFlags == kLocationInternal);
    const std::uint64_t blockOffset = isInternal ? entryIt->location.offset : 0;

    ParsedKrdi sourceKrdi;
    if (!ParseKrdiAt(containerBytes, blockOffset, &sourceKrdi, error)) {
        return false;
    }

    std::vector<std::byte> newBlock;
    if (!BuildModifiedKrdi(sourceKrdi, replacementData, &newBlock, error)) {
        return false;
    }

    std::uint64_t newOffset = 0;
    if (isInternal) {
        newOffset = AlignUp(static_cast<std::uint64_t>(containerBytes.size()), 16);
        if (newOffset > static_cast<std::uint64_t>(containerBytes.size())) {
            containerBytes.resize(static_cast<std::size_t>(newOffset), std::byte{0});
        }
        containerBytes.insert(containerBytes.end(), newBlock.begin(), newBlock.end());
    } else {
        containerBytes = std::move(newBlock);
        newOffset = 0;
    }

    RdbEntry updatedEntry = *entryIt;
    updatedEntry.fileSize = replacementData.size();
    if (!PatchEntryLocation(&updatedEntry, newOffset, static_cast<std::uint32_t>(newBlock.size()), error)) {
        return false;
    }

    if (!WriteWholeFile(containerPath, containerBytes, error)) {
        return false;
    }

    *entryIt = std::move(updatedEntry);
    return SaveRdb(error);
}

bool RdbTool::Insert(std::uint32_t newFileKtid, std::uint32_t templateFileKtid,
                     const fs::path& inputFilePath,
                     std::uint32_t typeInfoKtid,
                     bool reuseTemplateData,
                     std::string* error) {
    if (reuseTemplateData) {
        return Insert(newFileKtid, templateFileKtid, true, typeInfoKtid, error);
    }

    std::vector<std::byte> replacementData;
    if (!ReadWholeFile(inputFilePath, &replacementData, error)) {
        return false;
    }
    return Insert(newFileKtid, templateFileKtid, replacementData, typeInfoKtid, false, error);
}

bool RdbTool::Insert(std::uint32_t newFileKtid, std::uint32_t templateFileKtid,
                     std::span<const std::byte> replacementData,
                     std::uint32_t typeInfoKtid,
                     bool reuseTemplateData,
                     std::string* error) {
    if (reuseTemplateData) {
        return Insert(newFileKtid, templateFileKtid, true, typeInfoKtid, error);
    }

    if (FindEntryByFileKtid(newFileKtid) != nullptr) {
        SetError(error, "newFileKtid already exists in RDB.");
        return false;
    }

    const RdbEntry* templateEntry = FindEntryByFileKtid(templateFileKtid);
    if (templateEntry == nullptr) {
        SetError(error, "Template entry not found.");
        return false;
    }
    if (!templateEntry->hasLocation) {
        SetError(error, "Template entry has no location metadata.");
        return false;
    }

    fs::path templateContainerPath;
    std::vector<std::byte> templateContainerBytes;
    if (!ReadContainer(*templateEntry, &templateContainerPath, &templateContainerBytes, error)) {
        return false;
    }

    const bool isInternal = (templateEntry->location.newFlags == kLocationInternal);
    const std::uint64_t blockOffset = isInternal ? templateEntry->location.offset : 0;

    ParsedKrdi templateKrdi;
    if (!ParseKrdiAt(templateContainerBytes, blockOffset, &templateKrdi, error)) {
        return false;
    }

    std::vector<std::byte> newBlock;
    if (!BuildModifiedKrdi(templateKrdi, replacementData, &newBlock, error)) {
        return false;
    }

    RdbEntry newEntry = *templateEntry;
    newEntry.fileKtid = newFileKtid;
    newEntry.fileSize = replacementData.size();
    if (typeInfoKtid != 0) {
        newEntry.typeInfoKtid = typeInfoKtid;
    }

    std::uint64_t newOffset = 0;
    std::uint32_t newSize = static_cast<std::uint32_t>(newBlock.size());

    if (isInternal) {
        std::vector<std::byte> containerBytes = std::move(templateContainerBytes);
        newOffset = AlignUp(static_cast<std::uint64_t>(containerBytes.size()), 16);
        if (newOffset > static_cast<std::uint64_t>(containerBytes.size())) {
            containerBytes.resize(static_cast<std::size_t>(newOffset), std::byte{0});
        }
        containerBytes.insert(containerBytes.end(), newBlock.begin(), newBlock.end());
        if (!PatchEntryLocation(&newEntry, newOffset, newSize, error)) {
            return false;
        }
        if (!WriteWholeFile(templateContainerPath, containerBytes, error)) {
            return false;
        }
    } else {
        const std::string folderPrefix = Hex8(newFileKtid).substr(6, 2);
        const std::string relFile = "0x" + Hex8(newFileKtid) + ".file";
        const fs::path folder = fs::path(header_.FolderPath()) / folderPrefix;
        const fs::path relPath = folder / relFile;
        const fs::path absPath = packageDir_ / relPath;
        if (!PatchEntryLocation(&newEntry, 0, newSize, error)) {
            return false;
        }
        if (!WriteWholeFile(absPath, newBlock, error)) {
            return false;
        }
        newEntry.location.containerPath = relPath;
    }

    newEntry.index = entries_.size();
    newEntry.entryOffsetInRdb = 0;
    entries_.push_back(std::move(newEntry));
    header_.fileCount = static_cast<std::uint32_t>(entries_.size());

    if (!SaveRdb(error)) {
        return false;
    }
    return Reload(error);
}

bool RdbTool::Insert(std::uint32_t newFileKtid, std::uint32_t templateFileKtid,
                     bool reuseTemplateData,
                     std::uint32_t typeInfoKtid,
                     std::string* error) {
    if (!reuseTemplateData) {
        SetError(error, "Insert(reuse) overload requires reuseTemplateData=true.");
        return false;
    }

    if (FindEntryByFileKtid(newFileKtid) != nullptr) {
        SetError(error, "newFileKtid already exists in RDB.");
        return false;
    }

    const RdbEntry* templateEntry = FindEntryByFileKtid(templateFileKtid);
    if (templateEntry == nullptr) {
        SetError(error, "Template entry not found.");
        return false;
    }
    if (!templateEntry->hasLocation) {
        SetError(error, "Template entry has no location metadata.");
        return false;
    }
    if (templateEntry->location.newFlags == kLocationExternal) {
        SetError(error, "Reuse insert currently supports only internal (0x401) template entries.");
        return false;
    }

    RdbEntry newEntry = *templateEntry;
    newEntry.fileKtid = newFileKtid;
    if (typeInfoKtid != 0) {
        newEntry.typeInfoKtid = typeInfoKtid;
    }
    newEntry.index = entries_.size();
    newEntry.entryOffsetInRdb = 0;
    entries_.push_back(std::move(newEntry));
    header_.fileCount = static_cast<std::uint32_t>(entries_.size());

    if (!SaveRdb(error)) {
        return false;
    }
    return Reload(error);
}

bool RdbTool::ReadRdx(std::string* error) {
    std::vector<std::byte> bytes;
    if (!ReadWholeFile(rootRdxPath_, &bytes, error)) {
        return false;
    }
    if ((bytes.size() % 8u) != 0) {
        SetError(error, "Invalid RDX size (must be divisible by 8).");
        return false;
    }

    binary_io::span_istream stream(std::span<const std::byte>(bytes.data(), bytes.size()));
    const std::size_t count = bytes.size() / 8u;
    rdxEntries_.clear();
    rdxEntries_.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        RdxEntry entry{};
        if (!ReadValues(stream, error, entry.index, entry.marker, entry.fileId)) {
            return false;
        }
        rdxEntries_.push_back(entry);
    }
    return true;
}

bool RdbTool::ReadRdb(std::string* error) {
    std::vector<std::byte> bytes;
    if (!ReadWholeFile(rootRdbPath_, &bytes, error)) {
        return false;
    }
    if (bytes.size() < 32) {
        SetError(error, "RDB file is too small.");
        return false;
    }

    binary_io::span_istream stream(std::span<const std::byte>(bytes.data(), bytes.size()));

    std::array<std::byte, 4> magicBytes{};
    if (!ReadBytes(stream, magicBytes, error)) {
        return false;
    }
    for (std::size_t i = 0; i < magicBytes.size(); ++i) {
        header_.magic[i] = static_cast<char>(magicBytes[i]);
    }
    if (std::string(header_.magic.data(), header_.magic.size()) != "_DRK") {
        SetError(error, "Invalid RDB magic.");
        return false;
    }

    if (!ReadValues(stream, error,
                    header_.version,
                    header_.headerSize,
                    header_.systemId,
                    header_.fileCount,
                    header_.databaseId)) {
        return false;
    }

    std::array<std::byte, 8> folderBytes{};
    if (!ReadBytes(stream, folderBytes, error)) {
        return false;
    }
    for (std::size_t i = 0; i < folderBytes.size(); ++i) {
        header_.folderPathRaw[i] = static_cast<char>(folderBytes[i]);
    }

    entries_.clear();
    entries_.reserve(header_.fileCount);

    for (std::uint32_t index = 0; index < header_.fileCount; ++index) {
        while ((stream.tell() & 3) != 0) {
            std::array<std::byte, 1> skip{};
            if (!ReadBytes(stream, skip, error)) {
                return false;
            }
        }

        RdbEntry entry{};
        entry.index = index;
        entry.entryOffsetInRdb = static_cast<std::uint64_t>(stream.tell());

        std::array<std::byte, 4> entryMagic{};
        if (!ReadBytes(stream, entryMagic, error)) {
            return false;
        }
        for (std::size_t i = 0; i < entryMagic.size(); ++i) {
            entry.magic[i] = static_cast<char>(entryMagic[i]);
        }

        if (!ReadValues(stream, error,
                        entry.version,
                        entry.entrySize,
                        entry.dataSize,
                        entry.fileSize,
                        entry.entryType,
                        entry.fileKtid,
                        entry.typeInfoKtid,
                        entry.flags)) {
            return false;
        }

        if (entry.entrySize < kRdbEntryHeaderSize) {
            SetError(error, "Invalid RDB entry size.");
            return false;
        }
        if (entry.dataSize > entry.entrySize) {
            SetError(error, "Invalid RDB metadata size.");
            return false;
        }

        const std::uint64_t payloadWithMeta = entry.entrySize - kRdbEntryHeaderSize;
        if (entry.dataSize > payloadWithMeta) {
            SetError(error, "RDB entry metadata exceeds entry payload.");
            return false;
        }
        const std::uint64_t paramSize64 = payloadWithMeta - entry.dataSize;
        std::size_t paramSize = 0;
        std::size_t metadataSize = 0;
        if (!ToSizeT(paramSize64, &paramSize) || !ToSizeT(entry.dataSize, &metadataSize)) {
            SetError(error, "RDB entry is too large.");
            return false;
        }

        entry.paramBlock.resize(paramSize);
        if (paramSize > 0 && !ReadBytes(stream, std::span<std::byte>(entry.paramBlock.data(), entry.paramBlock.size()), error)) {
            return false;
        }
        entry.metadataBlock.resize(metadataSize);
        if (metadataSize > 0 && !ReadBytes(stream, std::span<std::byte>(entry.metadataBlock.data(), entry.metadataBlock.size()), error)) {
            return false;
        }

        if (!ParseEntryLocation(&entry)) {
            SetError(error, "Failed to parse RDB entry location.");
            return false;
        }
        if (!ResolveContainerPath(&entry)) {
            SetError(error, "Failed to resolve RDB entry container path.");
            return false;
        }

        entries_.push_back(std::move(entry));
    }

    return true;
}

bool RdbTool::ResolveContainerPath(RdbEntry* entry) const {
    if (!entry->hasLocation) {
        return true;
    }

    if (entry->location.newFlags == kLocationExternal) {
        std::string folderPath = header_.FolderPath();
        if (folderPath.empty()) {
            folderPath = "data/";
        }
        const std::string folderPrefix = Hex8(entry->fileKtid).substr(6, 2);
        entry->location.containerPath = fs::path(folderPath) / folderPrefix / ("0x" + Hex8(entry->fileKtid) + ".file");
        return true;
    }

    const auto it = std::find_if(rdxEntries_.begin(), rdxEntries_.end(), [entry](const RdxEntry& rdx) {
        return rdx.index == entry->location.fdataId;
    });
    if (it == rdxEntries_.end()) {
        return false;
    }

    entry->location.containerPath = fs::path("0x" + Hex8(it->fileId) + ".fdata");
    return true;
}

bool RdbTool::ParseEntryLocation(RdbEntry* entry) const {
    entry->hasLocation = false;
    entry->location = {};

    if (entry->metadataBlock.empty()) {
        return true;
    }

    binary_io::span_istream stream(std::span<const std::byte>(entry->metadataBlock.data(), entry->metadataBlock.size()));

    if (entry->metadataBlock.size() == 0x11) {
        std::uint16_t newFlags = 0;
        std::uint8_t highByte = 0;
        std::uint8_t skip0 = 0;
        std::uint8_t skip1 = 0;
        std::uint8_t skip2 = 0;
        std::uint32_t lowBytes = 0;
        std::uint32_t sizeInContainer = 0;
        std::uint16_t fdataId = 0;
        std::uint8_t tailPad = 0;

        if (!ReadValues(stream, nullptr, newFlags, highByte, skip0, skip1, skip2, lowBytes, sizeInContainer, fdataId, tailPad)) {
            return false;
        }

        entry->location.newFlags = newFlags;
        entry->location.offset = (static_cast<std::uint64_t>(highByte) << 32) | lowBytes;
        entry->location.sizeInContainer = sizeInContainer;
        entry->location.fdataId = fdataId;
        entry->location.uses64BitOffset = true;
        entry->hasLocation = true;
        return true;
    }

    if (entry->metadataBlock.size() == 0x0D) {
        std::uint16_t newFlags = 0;
        std::uint32_t offset32 = 0;
        std::uint32_t sizeInContainer = 0;
        std::uint16_t fdataId = 0;
        std::uint8_t tailPad = 0;
        if (!ReadValues(stream, nullptr, newFlags, offset32, sizeInContainer, fdataId, tailPad)) {
            return false;
        }

        entry->location.newFlags = newFlags;
        entry->location.offset = offset32;
        entry->location.sizeInContainer = sizeInContainer;
        entry->location.fdataId = fdataId;
        entry->location.uses64BitOffset = false;
        entry->hasLocation = true;
        return true;
    }

    return true;
}

bool RdbTool::ReadContainer(const RdbEntry& entry, fs::path* resolvedPath,
                            std::vector<std::byte>* outBytes, std::string* error) const {
    if (!entry.hasLocation) {
        SetError(error, "Entry has no location.");
        return false;
    }
    const fs::path fullPath = packageDir_ / entry.location.containerPath;
    if (resolvedPath != nullptr) {
        *resolvedPath = fullPath;
    }
    return ReadWholeFile(fullPath, outBytes, error);
}

bool RdbTool::ParseKrdiAt(const std::vector<std::byte>& containerBytes, std::uint64_t offset,
                          ParsedKrdi* outKrdi, std::string* error) const {
    if (outKrdi == nullptr) {
        SetError(error, "Invalid output pointer for KRDI parse.");
        return false;
    }

    std::size_t offsetSize = 0;
    if (!ToSizeT(offset, &offsetSize) || offsetSize > containerBytes.size()) {
        SetError(error, "KRDI offset is out of range.");
        return false;
    }
    if ((containerBytes.size() - offsetSize) < kKrdiHeaderSize) {
        SetError(error, "Not enough data for KRDI header.");
        return false;
    }

    binary_io::span_istream stream(std::span<const std::byte>(containerBytes.data(), containerBytes.size()));
    stream.seek_absolute(static_cast<binary_io::streamoff>(offset));

    std::array<std::byte, 4> magicBytes{};
    if (!ReadBytes(stream, magicBytes, error)) {
        return false;
    }
    std::array<char, 4> magic{};
    for (std::size_t i = 0; i < magicBytes.size(); ++i) {
        magic[i] = static_cast<char>(magicBytes[i]);
    }
    if (std::string(magic.data(), magic.size()) != "IDRK") {
        SetError(error, "KRDI magic mismatch.");
        return false;
    }

    std::array<std::byte, 4> versionBytes{};
    if (!ReadBytes(stream, versionBytes, error)) {
        return false;
    }

    ParsedKrdi parsed{};
    parsed.header.magic = magic;
    for (std::size_t i = 0; i < versionBytes.size(); ++i) {
        parsed.header.version[i] = static_cast<char>(versionBytes[i]);
    }

    if (!ReadValues(stream, error,
                    parsed.header.allBlockSize,
                    parsed.header.compressedSize,
                    parsed.header.uncompressedSize,
                    parsed.header.paramDataSize,
                    parsed.header.hashName,
                    parsed.header.hashType,
                    parsed.header.flags,
                    parsed.header.resourceId,
                    parsed.header.paramCount)) {
        return false;
    }

    if (parsed.header.paramCount < 0) {
        SetError(error, "KRDI paramCount is negative.");
        return false;
    }

    const std::uint64_t paramTableSize = static_cast<std::uint64_t>(parsed.header.paramCount) * 12u;
    const std::uint64_t paramSectionSize64 = paramTableSize + parsed.header.paramDataSize;
    std::size_t paramSectionSize = 0;
    if (!ToSizeT(paramSectionSize64, &paramSectionSize)) {
        SetError(error, "KRDI param section is too large.");
        return false;
    }

    parsed.payloadOffset = offset + kKrdiHeaderSize + paramSectionSize64;
    if (parsed.payloadOffset > static_cast<std::uint64_t>(containerBytes.size())) {
        SetError(error, "KRDI payload offset exceeds container size.");
        return false;
    }
    if (parsed.header.allBlockSize < (kKrdiHeaderSize + paramSectionSize64)) {
        SetError(error, "KRDI allBlockSize is invalid.");
        return false;
    }
    if ((offset + parsed.header.allBlockSize) > static_cast<std::uint64_t>(containerBytes.size())) {
        SetError(error, "KRDI block exceeds container size.");
        return false;
    }

    parsed.paramSection.resize(paramSectionSize);
    if (paramSectionSize > 0 &&
        !ReadBytes(stream, std::span<std::byte>(parsed.paramSection.data(), parsed.paramSection.size()), error)) {
        return false;
    }

    *outKrdi = std::move(parsed);
    return true;
}

bool RdbTool::ExtractPayload(const std::vector<std::byte>& containerBytes,
                             const ParsedKrdi& krdi, std::vector<std::byte>* outPayload,
                             std::string* error) const {
    outPayload->clear();

    std::size_t cursor = 0;
    if (!ToSizeT(krdi.payloadOffset, &cursor) || cursor > containerBytes.size()) {
        SetError(error, "KRDI payload offset overflow.");
        return false;
    }

    std::size_t uncompressedSize = 0;
    if (!ToSizeT(krdi.header.uncompressedSize, &uncompressedSize)) {
        SetError(error, "KRDI uncompressed size is too large.");
        return false;
    }

    const std::uint32_t compressionType = (krdi.header.flags >> 20) & 0x3F;
    if (compressionType == kCompressionZlib || compressionType == kCompressionExtended) {
        outPayload->reserve(uncompressedSize);

        while (outPayload->size() < uncompressedSize) {
            std::uint32_t zSize = 0;
            if (compressionType == kCompressionExtended) {
                if ((cursor + 10u) > containerBytes.size()) {
                    SetError(error, "Extended zlib chunk header exceeds payload bounds.");
                    return false;
                }
                zSize = static_cast<std::uint32_t>(containerBytes[cursor + 0]) |
                        (static_cast<std::uint32_t>(containerBytes[cursor + 1]) << 8);
                cursor += 10;
            } else {
                if ((cursor + 4u) > containerBytes.size()) {
                    SetError(error, "zlib chunk header exceeds payload bounds.");
                    return false;
                }
                zSize = static_cast<std::uint32_t>(containerBytes[cursor + 0]) |
                        (static_cast<std::uint32_t>(containerBytes[cursor + 1]) << 8) |
                        (static_cast<std::uint32_t>(containerBytes[cursor + 2]) << 16) |
                        (static_cast<std::uint32_t>(containerBytes[cursor + 3]) << 24);
                cursor += 4;
            }

            if (zSize == 0 || zSize == 0xFFFFFFFFu) {
                break;
            }
            if ((cursor + zSize) > containerBytes.size()) {
                SetError(error, "zlib chunk payload exceeds container bounds.");
                return false;
            }

            const std::size_t remain = uncompressedSize - outPayload->size();
            const std::size_t expected = std::min(remain, kDefaultChunkSize);
            std::vector<std::byte> chunkOut;
            if (!InflateZlibChunk(
                    std::span<const std::byte>(containerBytes.data() + cursor, zSize),
                    expected,
                    &chunkOut,
                    error)) {
                return false;
            }

            outPayload->insert(outPayload->end(), chunkOut.begin(), chunkOut.end());
            cursor += zSize;
        }

        if (outPayload->size() != uncompressedSize) {
            SetError(error, "Decompressed payload size mismatch.");
            return false;
        }
        return true;
    }

    std::size_t copySize = uncompressedSize;
    if (compressionType == kCompressionEncrypted || compressionType == 0) {
        copySize = uncompressedSize;
    }
    if ((cursor + copySize) > containerBytes.size()) {
        SetError(error, "Raw payload exceeds container bounds.");
        return false;
    }

    outPayload->assign(containerBytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                       containerBytes.begin() + static_cast<std::ptrdiff_t>(cursor + copySize));
    return true;
}

bool RdbTool::BuildModifiedKrdi(const ParsedKrdi& source,
                                std::span<const std::byte> replacementData,
                                std::vector<std::byte>* outBlock,
                                std::string* error) const {
    RdbTool::KrdiHeader h = source.header;
    h.flags &= ~kCompressionMask;
    h.compressedSize = replacementData.size();
    h.uncompressedSize = replacementData.size();
    h.allBlockSize = kKrdiHeaderSize + source.paramSection.size() + replacementData.size();

    binary_io::memory_ostream out;
    out.write_bytes(std::as_bytes(std::span(h.magic)));
    out.write_bytes(std::as_bytes(std::span(h.version)));
    out.write(h.allBlockSize,
              h.compressedSize,
              h.uncompressedSize,
              h.paramDataSize,
              h.hashName,
              h.hashType,
              h.flags,
              h.resourceId,
              h.paramCount);
    if (!source.paramSection.empty()) {
        out.write_bytes(std::span<const std::byte>(source.paramSection.data(), source.paramSection.size()));
    }
    if (!replacementData.empty()) {
        out.write_bytes(replacementData);
    }

    std::vector<std::byte> block = std::move(out.rdbuf());
    if (block.size() != h.allBlockSize) {
        SetError(error, "Built KRDI block size mismatch.");
        return false;
    }
    *outBlock = std::move(block);
    return true;
}

bool RdbTool::PatchEntryLocation(RdbEntry* entry, std::uint64_t newOffset, std::uint32_t newSize, std::string* error) const {
    if (entry == nullptr || !entry->hasLocation) {
        SetError(error, "Entry has no patchable location.");
        return false;
    }

    entry->location.offset = newOffset;
    entry->location.sizeInContainer = newSize;

    if (entry->metadataBlock.size() == 0x11) {
        if (newOffset > 0xFF'FFFF'FFFFull) {
            SetError(error, "Offset exceeds 40-bit location encoding limit.");
            return false;
        }
        WriteU16LE(entry->metadataBlock.data() + 0, entry->location.newFlags);
        entry->metadataBlock[2] = static_cast<std::byte>((newOffset >> 32) & 0xFFu);
        WriteU32LE(entry->metadataBlock.data() + 6, static_cast<std::uint32_t>(newOffset & 0xFFFFFFFFu));
        WriteU32LE(entry->metadataBlock.data() + 10, newSize);
        return true;
    }

    if (entry->metadataBlock.size() == 0x0D) {
        if (newOffset > 0xFFFFFFFFull) {
            SetError(error, "Offset exceeds 32-bit location encoding limit.");
            return false;
        }
        WriteU16LE(entry->metadataBlock.data() + 0, entry->location.newFlags);
        WriteU32LE(entry->metadataBlock.data() + 2, static_cast<std::uint32_t>(newOffset));
        WriteU32LE(entry->metadataBlock.data() + 6, newSize);
        return true;
    }

    SetError(error, "Unsupported location metadata size for patching.");
    return false;
}

bool RdbTool::SaveRdb(std::string* error) {
    binary_io::memory_ostream out;

    header_.fileCount = static_cast<std::uint32_t>(entries_.size());

    out.write_bytes(std::as_bytes(std::span(header_.magic)));
    out.write(header_.version,
              header_.headerSize,
              header_.systemId,
              header_.fileCount,
              header_.databaseId);
    out.write_bytes(std::as_bytes(std::span(header_.folderPathRaw)));

    for (std::size_t i = 0; i < entries_.size(); ++i) {
        while ((out.tell() & 3) != 0) {
            const std::array<std::byte, 1> pad{std::byte{0}};
            out.write_bytes(pad);
        }

        RdbEntry& entry = entries_[i];
        entry.index = i;
        entry.entryOffsetInRdb = static_cast<std::uint64_t>(out.tell());
        entry.dataSize = entry.metadataBlock.size();
        entry.entrySize = kRdbEntryHeaderSize + entry.paramBlock.size() + entry.metadataBlock.size();

        out.write_bytes(std::as_bytes(std::span(entry.magic)));
        out.write(entry.version,
                  entry.entrySize,
                  entry.dataSize,
                  entry.fileSize,
                  entry.entryType,
                  entry.fileKtid,
                  entry.typeInfoKtid,
                  entry.flags);
        if (!entry.paramBlock.empty()) {
            out.write_bytes(std::span<const std::byte>(entry.paramBlock.data(), entry.paramBlock.size()));
        }
        if (!entry.metadataBlock.empty()) {
            out.write_bytes(std::span<const std::byte>(entry.metadataBlock.data(), entry.metadataBlock.size()));
        }
    }

    const std::vector<std::byte> bytes = std::move(out.rdbuf());
    return WriteWholeFile(rootRdbPath_, bytes, error);
}

bool RdbTool::ReadWholeFile(const fs::path& path, std::vector<std::byte>* outBytes, std::string* error) {
    if (outBytes == nullptr) {
        SetError(error, "Invalid output buffer.");
        return false;
    }

    std::error_code ec;
    if (!fs::exists(path, ec)) {
        SetError(error, "File does not exist: " + path.string());
        return false;
    }
    if (!fs::is_regular_file(path, ec)) {
        SetError(error, "Not a regular file: " + path.string());
        return false;
    }

    std::uint64_t fileSize64 = fs::file_size(path, ec);
    if (ec) {
        SetError(error, "Failed to query file size: " + path.string());
        return false;
    }

    std::size_t fileSize = 0;
    if (!ToSizeT(fileSize64, &fileSize)) {
        SetError(error, "File too large for this process: " + path.string());
        return false;
    }

    outBytes->clear();
    outBytes->resize(fileSize);

    try {
        binary_io::file_istream in(path);
        if (fileSize > 0) {
            in.read_bytes(std::span<std::byte>(outBytes->data(), outBytes->size()));
        }
    } catch (const std::exception& ex) {
        SetError(error, std::string("Failed to read file: ") + ex.what());
        return false;
    }

    return true;
}

bool RdbTool::WriteWholeFile(const fs::path& path, std::span<const std::byte> bytes, std::string* error) {
    std::error_code ec;
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            SetError(error, "Failed to create parent directory for file: " + path.string());
            return false;
        }
    }

    try {
        binary_io::file_ostream out(path, binary_io::write_mode::truncate);
        if (!bytes.empty()) {
            out.write_bytes(bytes);
        }
        out.flush();
    } catch (const std::exception& ex) {
        SetError(error, std::string("Failed to write file: ") + ex.what());
        return false;
    }

    return true;
}

}  // namespace LooseFileLoader

#include "Relocation.h"
#define NOMINMAX
#include <Windows.h>
#include <cstdint>
#include <PluginAPI.h>
#include <LogUtils.h>
#include <FileUtils.h>
#include <HookUtils.h>
#include <BranchTrampoline.h>
#include <GameType.h>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <memory>
#include "binary_io/binary_io.hpp"

namespace fs = std::filesystem;
#define PLUGIN_NAME "LooseFileLoader"
#define PLUGIN_VERSION_MAJOR 1
#define PLUGIN_VERSION_MINOR 0
#define PLUGIN_VERSION_PATCH 0

#pragma pack(push, 1)

struct RdbRuntimeEntryDesc {
  uint32_t magic;        // +0x00, "IDRK" (0x14062BBF8 初始化 + 0x140270A44读取)
  uint32_t version;      // +0x04
  uint64_t sizeInContainer;    // +0x08, allBlockSize in rdata
  uint64_t compressedSize; // +0x10
  uint64_t fileSize;     // +0x18 uncompressedSize in rdata
  uint32_t fileKtid;     // +0x20
  uint32_t typeInfoKtid; // +0x24, 0x14038C1A4 用于 TypeHandler 查找
  uint32_t flags;        // +0x28, 0x14038B5F0: (flags>>20)&0x3F 压缩模式
  uint32_t f2C;          // +0x2C
  uint32_t paramCount; // +0x30, 0x14038C1A4 使用
  uint32_t f34;          // +0x34, 当前路径未稳定使用
  void*    paramDataBlock;  // +0x38
};
static_assert(sizeof(RdbRuntimeEntryDesc) == 0x40);
#pragma pack(pop)
// cmake --build build --config Release --target LooseFileLoader
namespace {
/*
__int64 __fastcall sub_140FCABD8(__int64 a1)
{
  if ( dword_144402DA8 > *(_DWORD *)(*(_QWORD *)NtCurrentTeb()->ThreadLocalStoragePointer + 24LL) )
  {
    Init_thread_header(&dword_144402DA8);
    if ( dword_144402DA8 == -1 )
    {
      dword_144402DB8 = 0;
      qword_144402DB0 = (__int64)off_143835828;
      qword_144402DC0 = (__int64)"ObjectDatabaseFile";
      atexit(sub_141D93C00);
      Init_thread_footer(&dword_144402DA8);
    }
  }
  return sub_140AC53E8(a1, 0x20A6A0BBLL, &qword_144402DB0);
}
*/

struct AssetDescriptor {
  std::uintptr_t vtable;
  std::uintptr_t f08;
  const char * assetName;
};
static_assert(sizeof(AssetDescriptor) == 0x18);

// ------------------------------ RDB / RDX -----------------------------------

struct RdxEntry {
    std::uint16_t index = 0;
    std::uint16_t marker = 0;
    std::uint32_t fileId = 0;
  };
  
  enum class StorageKind : std::uint8_t {
    InternalFdata,
    ExternalFile,
  };
  
  struct RdbLocation {
    StorageKind storage = StorageKind::InternalFdata;
    std::uint64_t offset = 0;
    std::uint32_t sizeInContainer = 0;
    std::uint16_t fdataId = 0;
    fs::path containerPath;
  };
  
  struct RdbEntry {
    std::uint32_t fileKtid = 0;
    std::uint32_t entryType = 0;
    std::uint32_t typeInfoKtid = 0;
    std::uint64_t entrySize = 0;
    std::uint64_t dataSize = 0;
    std::uint64_t fileSize = 0;
    std::uint32_t flags = 0;
    RdbLocation location;
  };

  struct RdbFileHeader {
    std::int32_t version = 0;
    std::int32_t headerSize = 0;
    std::int32_t systemId = 0;
    std::int32_t fileCount = 0;
    std::uint32_t databaseId = 0;
  };

  struct KrdiHeader {
    char magic[4] = {};
    char version[4] = {};
    std::uint64_t allBlockSize = 0;
    std::uint64_t compressedSize = 0;
    std::uint64_t uncompressedSize = 0;
    std::int32_t paramDataSize = 0;
    std::int32_t hashName = 0;
    std::uint32_t hashType = 0;
    std::uint32_t flags = 0;
    std::uint32_t resourceId = 0;
    std::int32_t paramCount = 0;
  };
  
  class ArchiveIndex {
   public:
    bool LoadRdx(const fs::path& rdxPath) {
      std::ifstream in(rdxPath, std::ios::binary);
      if (!in) {
        return false;
      }
  
      rdx_.clear();
      while (true) {
        RdxEntry e;
        in.read(reinterpret_cast<char*>(&e.index), sizeof(e.index));
        if (!in) {
          break;
        }
        in.read(reinterpret_cast<char*>(&e.marker), sizeof(e.marker));
        in.read(reinterpret_cast<char*>(&e.fileId), sizeof(e.fileId));
        if (!in) {
          return false;
        }
        rdx_[e.index] = e.fileId;
      }
      return true;
    }
  
    bool LoadRdb(const fs::path& rdbPath) {
      std::ifstream in(rdbPath, std::ios::binary);
      if (!in) {
        return false;
      }
  
      char magic[4] = {};
      in.read(magic, 4);
      if (!in || std::string(magic, 4) != "_DRK") {
        return false;
      }
  
      std::int32_t version = 0;
      std::int32_t headerSize = 0;
      std::int32_t systemId = 0;
      std::int32_t fileCount = 0;
      std::uint32_t databaseId = 0;
      char folderRaw[8] = {};
  
      in.read(reinterpret_cast<char*>(&version), sizeof(version));
      in.read(reinterpret_cast<char*>(&headerSize), sizeof(headerSize));
      in.read(reinterpret_cast<char*>(&systemId), sizeof(systemId));
      in.read(reinterpret_cast<char*>(&fileCount), sizeof(fileCount));
      in.read(reinterpret_cast<char*>(&databaseId), sizeof(databaseId));
      in.read(folderRaw, sizeof(folderRaw));
  
      if (!in || fileCount < 0) {
        return false;
      }

      header_.version = version;
      header_.headerSize = headerSize;
      header_.systemId = systemId;
      header_.fileCount = fileCount;
      header_.databaseId = databaseId;
  
      folderPrefix_ = std::string(folderRaw, folderRaw + sizeof(folderRaw));
      folderPrefix_.erase(std::find(folderPrefix_.begin(), folderPrefix_.end(), '\0'), folderPrefix_.end());
  
      entries_.clear();
      byKtid_.clear();
      entries_.reserve(static_cast<std::size_t>(fileCount));
  
      constexpr std::size_t kRdbHeaderSize = 48;
      for (std::int32_t i = 0; i < fileCount; ++i) {
        // 4-byte alignment before each entry.
        while ((static_cast<std::uint64_t>(in.tellg()) % 4u) != 0u) {
          in.get();
        }
  
        char emagic[4] = {};
        std::uint32_t ever = 0;
        RdbEntry e;
  
        in.read(emagic, 4);
        in.read(reinterpret_cast<char*>(&ever), sizeof(ever));
        in.read(reinterpret_cast<char*>(&e.entrySize), sizeof(e.entrySize));
        in.read(reinterpret_cast<char*>(&e.dataSize), sizeof(e.dataSize));
        in.read(reinterpret_cast<char*>(&e.fileSize), sizeof(e.fileSize));
        in.read(reinterpret_cast<char*>(&e.entryType), sizeof(e.entryType));
        in.read(reinterpret_cast<char*>(&e.fileKtid), sizeof(e.fileKtid));
        in.read(reinterpret_cast<char*>(&e.typeInfoKtid), sizeof(e.typeInfoKtid));
        in.read(reinterpret_cast<char*>(&e.flags), sizeof(e.flags));
  
        if (!in || std::string(emagic, 4) != "IDRK") {
          return false;
        }
  
        const std::int64_t paramsSize =
            static_cast<std::int64_t>(e.entrySize) - static_cast<std::int64_t>(e.dataSize) - static_cast<std::int64_t>(kRdbHeaderSize);
        if (paramsSize > 0) {
          in.seekg(paramsSize, std::ios::cur);
        }
  
        if (e.dataSize == 0x11) {
          std::uint16_t newFlags = 0;
          std::uint8_t highByte = 0;
          std::uint8_t pad[3] = {};
          std::uint32_t low32 = 0;
          in.read(reinterpret_cast<char*>(&newFlags), sizeof(newFlags));
          in.read(reinterpret_cast<char*>(&highByte), sizeof(highByte));
          in.read(reinterpret_cast<char*>(pad), sizeof(pad));
          in.read(reinterpret_cast<char*>(&low32), sizeof(low32));
          e.location.offset = (static_cast<std::uint64_t>(highByte) << 32) | low32;
          in.read(reinterpret_cast<char*>(&e.location.sizeInContainer), sizeof(e.location.sizeInContainer));
          in.read(reinterpret_cast<char*>(&e.location.fdataId), sizeof(e.location.fdataId));
          in.get();
          e.location.storage = (newFlags == 0xC01) ? StorageKind::ExternalFile : StorageKind::InternalFdata;
        } else if (e.dataSize == 0x0D) {
          std::uint16_t newFlags = 0;
          std::uint32_t off32 = 0;
          in.read(reinterpret_cast<char*>(&newFlags), sizeof(newFlags));
          in.read(reinterpret_cast<char*>(&off32), sizeof(off32));
          e.location.offset = off32;
          in.read(reinterpret_cast<char*>(&e.location.sizeInContainer), sizeof(e.location.sizeInContainer));
          in.read(reinterpret_cast<char*>(&e.location.fdataId), sizeof(e.location.fdataId));
          in.get();
          e.location.storage = (newFlags == 0xC01) ? StorageKind::ExternalFile : StorageKind::InternalFdata;
        }
  
        e.location.containerPath = ResolveContainerPath(e);
        entries_.push_back(e);
        byKtid_[e.fileKtid] = entries_.size() - 1;
      }
  
      return true;
    }
  
    const RdbEntry* FindByKtid(std::uint32_t fileKtid) const {
      const auto it = byKtid_.find(fileKtid);
      if (it == byKtid_.end()) {
        return nullptr;
      }
      return &entries_[it->second];
    }

    const RdbFileHeader& Header() const {
      return header_;
    }

    const std::string& FolderPrefix() const {
      return folderPrefix_;
    }

    std::size_t RdxCount() const {
      return rdx_.size();
    }

    const std::vector<RdbEntry>& Entries() const {
      return entries_;
    }
  
   private:
    fs::path ResolveContainerPath(const RdbEntry& e) const {
      if (e.location.storage == StorageKind::ExternalFile) {
        const std::uint32_t hash = e.fileKtid;
        char folder[3] = {};
        std::snprintf(folder, sizeof(folder), "%02x", hash & 0xFFu);
  
        char fileName[32] = {};
        std::snprintf(fileName, sizeof(fileName), "0x%08x.file", hash);
  
        fs::path p = folderPrefix_;
        p /= folder;
        p /= fileName;
        return p;
      }
  
      const auto it = rdx_.find(e.location.fdataId);
      if (it == rdx_.end()) {
        return {};
      }
  
      char fdataName[32] = {};
      std::snprintf(fdataName, sizeof(fdataName), "0x%08x.fdata", it->second);
      return fs::path(fdataName);
    }
  
   private:
    RdbFileHeader header_{};
    std::string folderPrefix_;
    std::unordered_map<std::uint16_t, std::uint32_t> rdx_;
    std::vector<RdbEntry> entries_;
    std::unordered_map<std::uint32_t, std::size_t> byKtid_;
  };

  static const char* ToStorageString(StorageKind kind) {
    return kind == StorageKind::ExternalFile ? "ExternalFile" : "InternalFdata";
  }

  static std::string HexU32(std::uint32_t v) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::setw(8) << std::setfill('0') << v;
    return oss.str();
  }

  static std::string HexU64(std::uint64_t v) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::setw(16) << std::setfill('0') << v;
    return oss.str();
  }

  static bool ReadKrdiHeaderAt(std::ifstream& in, std::uint64_t offset, KrdiHeader* out) {
    if (out == nullptr) {
      return false;
    }

    in.clear();
    in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!in) {
      return false;
    }

    KrdiHeader h{};
    in.read(h.magic, sizeof(h.magic));
    in.read(h.version, sizeof(h.version));
    in.read(reinterpret_cast<char*>(&h.allBlockSize), sizeof(h.allBlockSize));
    in.read(reinterpret_cast<char*>(&h.compressedSize), sizeof(h.compressedSize));
    in.read(reinterpret_cast<char*>(&h.uncompressedSize), sizeof(h.uncompressedSize));
    in.read(reinterpret_cast<char*>(&h.paramDataSize), sizeof(h.paramDataSize));
    in.read(reinterpret_cast<char*>(&h.hashName), sizeof(h.hashName));
    in.read(reinterpret_cast<char*>(&h.hashType), sizeof(h.hashType));
    in.read(reinterpret_cast<char*>(&h.flags), sizeof(h.flags));
    in.read(reinterpret_cast<char*>(&h.resourceId), sizeof(h.resourceId));
    in.read(reinterpret_cast<char*>(&h.paramCount), sizeof(h.paramCount));
    if (!in) {
      return false;
    }

    if (std::memcmp(h.magic, "IDRK", 4) != 0) {
      return false;
    }

    *out = h;
    return true;
  }

  static bool DumpRootRdbInfo(const fs::path& packageDir, const fs::path& dumpPath) {
    const fs::path rdbPath = packageDir / "root.rdb";
    const fs::path rdxPath = packageDir / "root.rdx";

    ArchiveIndex index;
    if (!index.LoadRdx(rdxPath)) {
      _MESSAGE("LoadRdx failed: %s", rdxPath.string().c_str());
      return false;
    }
    if (!index.LoadRdb(rdbPath)) {
      _MESSAGE("LoadRdb failed: %s", rdbPath.string().c_str());
      return false;
    }

    std::ofstream out(dumpPath, std::ios::out | std::ios::trunc);
    if (!out) {
      _MESSAGE("Open dump file failed: %s", dumpPath.string().c_str());
      return false;
    }

    const auto& header = index.Header();
    const auto& entries = index.Entries();

    std::size_t externalCount = 0;
    std::size_t internalCount = 0;
    std::size_t unresolvedCount = 0;
    std::size_t krdiReadOkCount = 0;
    std::size_t krdiReadFailCount = 0;

    std::unordered_map<std::string, std::unique_ptr<std::ifstream>> streamCache;
    auto getContainerStream = [&](const fs::path& fullPath) -> std::ifstream* {
      const std::string key = fullPath.string();
      const auto it = streamCache.find(key);
      if (it != streamCache.end()) {
        return it->second.get();
      }

      auto in = std::make_unique<std::ifstream>(fullPath, std::ios::binary);
      if (!(*in)) {
        return nullptr;
      }

      auto* raw = in.get();
      streamCache.emplace(key, std::move(in));
      return raw;
    };

    for (const auto& e : entries) {
      if (e.location.storage == StorageKind::ExternalFile) {
        ++externalCount;
      } else {
        ++internalCount;
      }
      if (e.location.containerPath.empty()) {
        ++unresolvedCount;
      }
    }

    out << "# Nioh3 root.rdb dump\n";
    out << "package_dir: " << packageDir.string() << "\n";
    out << "rdb: " << rdbPath.string() << "\n";
    out << "rdx: " << rdxPath.string() << "\n";
    out << "folder_prefix: " << index.FolderPrefix() << "\n";
    out << "rdx_count: " << index.RdxCount() << "\n";
    out << "rdb_version: " << header.version << "\n";
    out << "rdb_header_size: " << header.headerSize << "\n";
    out << "rdb_system_id: " << header.systemId << "\n";
    out << "rdb_file_count(header): " << header.fileCount << "\n";
    out << "rdb_file_count(parsed): " << entries.size() << "\n";
    out << "rdb_database_id: " << HexU32(header.databaseId) << "\n";
    out << "entry_internal_count: " << internalCount << "\n";
    out << "entry_external_count: " << externalCount << "\n";
    out << "entry_unresolved_container_count: " << unresolvedCount << "\n";
    out << "\n";

    for (std::size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries[i];
      KrdiHeader krdi{};
      bool hasKrdi = false;
      if (!e.location.containerPath.empty()) {
        const fs::path fullContainerPath = packageDir / e.location.containerPath;
        if (auto* in = getContainerStream(fullContainerPath); in != nullptr) {
          const std::uint64_t readOffset = (e.location.storage == StorageKind::InternalFdata) ? e.location.offset : 0ull;
          hasKrdi = ReadKrdiHeaderAt(*in, readOffset, &krdi);
        }
      }
      if (hasKrdi) {
        ++krdiReadOkCount;
      } else {
        ++krdiReadFailCount;
      }

      out << "[" << i << "] "
          << "fileKtid=" << HexU32(e.fileKtid)
          << " entryType=" << HexU32(e.entryType)
          << " typeInfoKtid=" << HexU32(e.typeInfoKtid)
          << " flags=" << HexU32(e.flags)
          << " entrySize=" << HexU64(e.entrySize)
          << " dataSize=" << HexU64(e.dataSize)
          << " fileSize=" << HexU64(e.fileSize)
          << " storage=" << ToStorageString(e.location.storage)
          << " offset=" << HexU64(e.location.offset)
          << " sizeInContainer=" << HexU32(e.location.sizeInContainer)
          << " fdataId=" << e.location.fdataId
          << " containerPath=" << e.location.containerPath.string();

      if (hasKrdi) {
        const std::string krdiVer(krdi.version, krdi.version + 4);
        out << " krdiVersion=" << krdiVer
            << " allBlockSize=" << HexU64(krdi.allBlockSize)
            << " compressedSize=" << HexU64(krdi.compressedSize)
            << " uncompressedSize=" << HexU64(krdi.uncompressedSize)
            << " paramCount=" << krdi.paramCount
            << " paramDataSize=" << HexU32(static_cast<std::uint32_t>(krdi.paramDataSize))
            << " krdiFlags=" << HexU32(krdi.flags);
      } else {
        out << " krdiHeader=unavailable";
      }

      out
          << "\n";
    }

    out << "\n# KRDI read summary\n";
    out << "krdi_header_read_ok_count: " << krdiReadOkCount << "\n";
    out << "krdi_header_read_fail_count: " << krdiReadFailCount << "\n";

    out.flush();
    const bool ok = out.good();
    _MESSAGE("root.rdb dump: %s (%s)", dumpPath.string().c_str(), ok ? "ok" : "stream-error");
    return ok;
  }
}


namespace {


  class IAssetStreamReader {
    public:
      virtual ~IAssetStreamReader() = default;
      // +0x08, FileReader_Close_1405E6E6C
      // Release backend file handle and reset runtime state.
      virtual void Close() = 0;

      // +0x10, FileReader_Skip_1405E9C74
      // Relative skip forward/backward; returns moved bytes.
      virtual std::int64_t Skip(std::int64_t a_deltaBytes) = 0;

      // +0x18, FileReader_SkipAsync_1430ED9C0
      // Alternate skip path on async handle; returns moved bytes.
      virtual std::uint64_t ReadByte(std::uint8_t *a_outByte) = 0;

      // +0x20, FileReader_Read_1405EABD8
      // Read a_size bytes into (a_dst + a_dstOffset), return actual bytes read.
      virtual std::uint64_t Read(void* a_dst, std::uint64_t a_dstOffset, std::uint64_t a_size) = 0;

      // +0x28, FileReader_QueryCapability_142E74714
      // FileReader implementation returns 0 (unsupported/no-op capability).
      virtual std::uint64_t QueryCapability() = 0;
  };


  class AssetStreamProxyReader : public IAssetStreamReader {
    public:
      AssetStreamProxyReader(IAssetStreamReader* a_reader) : m_reader(a_reader) {}
      virtual ~AssetStreamProxyReader() override = default;
      virtual void Close() override { m_reader->Close(); }
      virtual std::int64_t Skip(std::int64_t a_deltaBytes) override {
        auto result = m_reader->Skip(a_deltaBytes); 
        //_MESSAGE("AssetStreamProxyReader::Skip expected: %lld, actual: %lld", a_deltaBytes, result);
        return result;
      }
      virtual std::uint64_t ReadByte(std::uint8_t *a_outByte) override { 
        auto result = m_reader->ReadByte(a_outByte); 
        _MESSAGE("AssetStreamProxyReader::ReadByte byte: %02X, actualRead: %d", *a_outByte, result);
        return result;
      }
      virtual std::uint64_t Read(void* a_dst, std::uint64_t a_dstOffset, std::uint64_t a_size) override { 
        auto result = m_reader->Read(a_dst, a_dstOffset, a_size); 
        //_MESSAGE("AssetStreamProxyReader::Read expected: %lld, actual: %lld", a_size, result);
        return result;
      }
      virtual std::uint64_t QueryCapability() override { 
        return m_reader->QueryCapability(); 
      }
    private:
      IAssetStreamReader* m_reader;
  };


  // Local file-backed stream reader that mirrors the game's StreamFileReader behavior:
  // - relative skip with clamped bounds
  // - single-byte read
  // - bulk read with dst offset
  // - directly uses binary_io::file_istream (no duplicated logical position state)
  class LooseModFileReader final : public IAssetStreamReader {
    public:
      LooseModFileReader() = delete;

      explicit LooseModFileReader(fs::path a_filePath) {
        Open(std::move(a_filePath));
      }

      ~LooseModFileReader() override {
        Close();
      }

      bool Open(fs::path a_filePath) {
        Close();
        m_filePath = std::move(a_filePath);

        if (m_filePath.empty() || !fs::exists(m_filePath) || !fs::is_regular_file(m_filePath)) {
          return false;
        }

        try {
          m_fileSize = fs::file_size(m_filePath);
          m_stream.open(m_filePath);
          m_stream.seek_absolute(0);
        } catch (...) {
          Close();
          return false;
        }
        return m_stream.is_open();
      }

      void Close() override {
        _MESSAGE("LooseModFileReader::Close");
        if (m_stream.is_open()) {
          m_stream.close();
        }
      }

      std::int64_t Skip(std::int64_t a_deltaBytes) override {
        _MESSAGE("LooseModFileReader::Skip deltaBytes: %lld", a_deltaBytes);
        if (!m_stream.is_open()) {
          return 0;
        }

        const std::int64_t cur = static_cast<std::int64_t>(m_stream.tell());
        const std::int64_t end = static_cast<std::int64_t>(m_fileSize);
        const std::int64_t target = std::clamp(cur + a_deltaBytes, std::int64_t{0}, end);

        m_stream.seek_absolute(static_cast<binary_io::streamoff>(target));
        return target - cur;
      }

      std::uint64_t ReadByte(std::uint8_t* a_outByte) override {
        _MESSAGE("LooseModFileReader::ReadByte");
        if (a_outByte == nullptr) {
          return 0;
        }
        return Read(a_outByte, 0, 1);
      }

      std::uint64_t Read(void* a_dst, std::uint64_t a_dstOffset, std::uint64_t a_size) override {
        _MESSAGE("LooseModFileReader::Read size: %lld", a_size);
        if (!m_stream.is_open() || a_dst == nullptr || a_size == 0) {
          return 0;
        }

        const std::uint64_t cur = static_cast<std::uint64_t>(m_stream.tell());
        const std::uint64_t remain = (cur < m_fileSize) ? (m_fileSize - cur) : 0;
        const std::uint64_t toRead = std::min(a_size, remain);
        if (toRead == 0) {
          return 0;
        }

        auto* const out = static_cast<std::byte*>(a_dst) + a_dstOffset;
        const auto before = m_stream.tell();
        try {
          m_stream.read_bytes(std::span<std::byte>(out, static_cast<std::size_t>(toRead)));
        } catch (const binary_io::buffer_exhausted&) {
          // Keep compatibility with game reader semantics: return actual bytes consumed.
        }
        const auto after = m_stream.tell();
        return (after > before) ? static_cast<std::uint64_t>(after - before) : 0;
      }

      std::uint64_t QueryCapability() override {
        return 0;
      }

      [[nodiscard]] std::uint64_t Tell() const {
        if (!m_stream.is_open()) {
          return 0;
        }
        return static_cast<std::uint64_t>(m_stream.tell());
      }

      [[nodiscard]] bool IsOpen() const {
        return m_stream.is_open();
      }

    private:
      fs::path m_filePath{};
      binary_io::file_istream m_stream{};
      std::uint64_t m_fileSize = 0;
  };


  class BaseGameAsset {
    public:
      virtual void Unk00() = 0;
      virtual void Unk08() = 0;
      virtual void Unk10() = 0;
      virtual const char*& GetTypeName(const char*& a_typeNameOut) = 0;
      virtual uint32_t GetTypeID() = 0;
      virtual void Unk28() = 0;
      virtual void Unk30() = 0;
      virtual void Unk38() = 0;
      virtual void Unk40() = 0;
      virtual void Unk48() = 0;
      virtual void Unk50() = 0;
      virtual void Unk58() = 0;
      virtual void Unk60() = 0;
      virtual void Unk68() = 0;
      virtual void Unk70() = 0;
      virtual void Unk78() = 0;
      virtual void Unk80() = 0;
      virtual void Unk88() = 0;
      virtual void Unk90() = 0;
      virtual void Unk98() = 0;
      virtual void UnkA0() = 0;
      virtual void UnkA8() = 0;
      virtual void* Deserialize(void* a_arg0, IAssetStreamReader* a_reader, void* a_arg2) = 0; // B0

      uintptr_t GetVtableAddr() const {
        return *reinterpret_cast<const uintptr_t*>(this) - RelocationManager::s_baseAddr + 0x140000000ull;
      }
  };


  fs::path g_gameRootDir;
  fs::path g_pluginsDir;
}

extern "C" __declspec(dllexport) bool nioh3_plugin_initialize(const Nioh3PluginInitializeParam * param) {
    _MESSAGE("Plugin initialized");
    _MESSAGE("Game version: %s", param->game_version_string);
    _MESSAGE("Plugin dir: %s", param->plugins_dir);
    g_gameRootDir = param->game_root_dir;
    g_pluginsDir = param->plugins_dir;

    using FnMountResource = bool(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, RdbRuntimeEntryDesc*, IAssetStreamReader*, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

    // E8 ? ? ? ? 48 8B 8F ? ? ? ? 48 8B 53
    auto addr = (FnMountResource)HookUtils::ScanIDAPattern("E8 ? ? ? ? 48 8B 8F ? ? ? ? 48 8B 53", 0, 1, 5);
    if (!addr) {
        _MESSAGE("VFS_MountResource not found");
        return false;
    }
    _MESSAGE("VFS_MountResource: %p", addr);

    // FF 93 ? ? ? ? 48 8D 4D
    auto addr2 = HookUtils::ScanIDAPattern("E8 ? ? ? ? 48 89 45 ? 48 85 C0 0F 84 ? ? ? ? 4C 8B 7F");
    _MESSAGE("addr2: %p", addr2);

    using FnRdbFindIndexByKtid = uint32_t(*)(uintptr_t, uintptr_t);

    static auto RdbFindIndexByKtidAddr = (FnRdbFindIndexByKtid)HookUtils::ScanIDAPattern("E8 ? ? ? ? BA ? ? ? ? 49 8D 8E ? ? ? ? 44 8B C2", 0, 1, 5);
    _MESSAGE("RdbFindIndexByKtidAddr: %p", RdbFindIndexByKtidAddr);

    using FnRdbGetTypeName = const char*(*)(uintptr_t, void*, uintptr_t);
    static auto RdbGetTypeNameAddr = (FnRdbGetTypeName)HookUtils::ScanIDAPattern("E8 ? ? ? ? 49 8D 8E ? ? ? ? 49 8B D5", 0, 1, 5);
    _MESSAGE("RdbGetTypeNameAddr: %p", RdbGetTypeNameAddr);

    // const fs::path packageDir = R"(E:\SteamLibrary\steamapps\common\Nioh3\package)";
    // fs::path dumpRoot = ".";
    // if (param && param->plugins_dir && std::strlen(param->plugins_dir) > 0) {
    //     dumpRoot = fs::path(param->plugins_dir);
    // }
    // const fs::path dumpPath = dumpRoot / "root_rdb_dump.txt";
    // if (!DumpRootRdbInfo(packageDir, dumpPath)) {
    //     _MESSAGE("DumpRootRdbInfo failed. packageDir=%s", packageDir.string().c_str());
    // }

    HookLambda(addr, [](uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7, RdbRuntimeEntryDesc* a8, IAssetStreamReader* a9, uintptr_t a10, uintptr_t a11, uintptr_t decompressor, uintptr_t a13, uintptr_t a14) -> bool {

        if (a3 != 0 && a7 > 0) {
            uint32_t realHash = RdbFindIndexByKtidAddr(a3 + 536, a7);
            uint32_t resType = *(uint32_t*)(a7 + 8);
            if (realHash != 0xFFFFFFFF) {
                // _MESSAGE("Load asset with hash: 0x%08X | Type: (0x%08X)", realHash, resType);
                // print a8
                // _MESSAGE("a8: %p", a8);
                // _MESSAGE("a8->magic: %08X", a8->magic);
                // _MESSAGE("a8->version: %08X", a8->version);
                // _MESSAGE("a8->sizeInContainer: %08X", a8->sizeInContainer);
                // _MESSAGE("a8->compressedSize: %08X", a8->compressedSize);
                // _MESSAGE("a8->uncompressedSize: %08X", a8->fileSize);
                // _MESSAGE("a8->fileKtid: %08X", a8->fileKtid);
                // _MESSAGE("a8->typeInfoKtid: %08X", a8->typeInfoKtid);
                // _MESSAGE("a8->flags: %d", a8->flags >> 20 & 0x3F);
                // _MESSAGE("a8->f2C: %08X", a8->f2C);
                // _MESSAGE("a8->paramCount: %08X", a8->paramCount);
                // _MESSAGE("a8->f34: %08X", a8->f34);
                // _MESSAGE("a8->paramDataBlock: %p", a8->paramDataBlock);
                if (realHash == 0xB9C7FD46) {
                  _MESSAGE("Load asset with hash: 0x%08X | Type: (0x%08X)", realHash, resType);
                  fs::path filePath = g_gameRootDir / "mods" / "0xB9C7FD46.g1m";
                  _MESSAGE("filePath: %s", filePath.string().c_str());
                  if (fs::exists(filePath)) {
                    auto oldFlags = a8->flags;
                    a8->flags = oldFlags | 0x100000;
                    _MESSAGE("Load asset from: %s", filePath.string().c_str());
                    auto reader = std::make_unique<LooseModFileReader>(filePath);
                    
                    auto ret = original(a1, a2, a3, a4, a5, a6, a7, a8, reader.get(), a10, a11, 0, a13, a14);

                    _MESSAGE("Load asset from: %s, ret: %d", filePath.string().c_str(), ret);
                    return ret;
                  }
                }
            }
            // IAssetStreamReader* assetStreamReader = new AssetStreamProxyReader(a9);
            // return original(a1, a2, a3, a4, a5, a6, a7, a8, assetStreamReader, a10, a11, a12, a13, a14);
        }
        return original(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, decompressor, a13, a14);
    });

    // E8 ? ? ? ? 84 C0 0F 84 ? ? ? ? 83 65 ? 00 48 8D 15
    auto addr3 = HookUtils::ScanIDAPattern("E8 ? ? ? ? 84 C0 0F 84 ? ? ? ? 83 65 ? 00 48 8D 15", 0, 1, 5);
    _MESSAGE("addr3: %p", addr3);

    using FnRegisterAsset = bool(*)(void *, uint32_t, BaseGameAsset*);

    FnRegisterAsset RegisterAsset = (FnRegisterAsset)addr3;

    // HookLambda(
    //     RegisterAsset,
    //     [](void *a_assetManager, uint32_t a_assetHash,
    //       BaseGameAsset *a_assetDescriptor) -> bool {
    //       char buf[256] = {0};
    //       if (a_assetDescriptor) {
    //         do {
    //           __try {
    //             const char *typeName = nullptr;
    //             a_assetDescriptor->GetTypeName(typeName);
    //             if (typeName) {
    //               snprintf(buf, sizeof(buf), "%s", typeName);
    //             } else {
    //               snprintf(buf, sizeof(buf), "Unknown");
    //             }
    //           } __except (EXCEPTION_EXECUTE_HANDLER) {
    //             // _MESSAGE("Exception: %d", GetExceptionCode());
    //             break;
    //           }
    //           _MESSAGE("Asset hash: 0x%08X | %p | %s ", a_assetHash,
    //                    a_assetDescriptor->GetVtableAddr(), buf);
    //         } while (0);
    //       }

    //       return original(a_assetManager, a_assetHash, a_assetDescriptor);
    //     });
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

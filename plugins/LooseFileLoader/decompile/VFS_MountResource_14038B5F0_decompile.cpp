// Readable reconstruction for Nioh3 fdata loading + VFS mount flow.
//
// Main path covered in this file:
// 1) RDB/RDX lookup -> resolve container path and block descriptor
// 2) Open container (.fdata/.file), seek to payload (offset + headerSkip)
// 3) Read exact bytes (sub_14026FBA0-like behavior)
// 4) Build segment table/context and call MountResource
//
// Function mapping to IDA symbols:
// - Rdb_FormatContainerPath_14062C918 / Rdb_ResolveContainerAndBlock_14062C4F8:
//     resolve container path + offset/span metadata
// - Rdb_ReadPayloadThenMount_14062B724:
//     read stage orchestrator, then call mount
// - Stream_ReadExactAndBindBuffer_14026FBA0 + VfsStream_Read_14062EC48:
//     exact-size read path
// - Vfs_OpenContainerStream_14062C304 + FsHandle_Seek_1400D529C:
//     open container stream and seek to payload
// - VFS_MountResource_14038B5F0:
//     mount resource
// - TypeHandler_FindByTypeId_14038A594:
//     type handler lookup
// - Mount_BuildSegmentTable_14038C1A4:
//     segment table build
// - Rdb_FindResHashByKtid_1413F13F0:
//     resolve resource hash from entry pointer

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nioh3::vfs {

namespace fs = std::filesystem;

// ----------------------------- Stream Layer ---------------------------------

struct StreamLike {
  virtual ~StreamLike() = default;
  virtual bool Seek(std::uint64_t absolute) = 0;
  virtual std::size_t Read(void* dst, std::size_t size) = 0;
  virtual std::uint64_t Tell() const = 0;
};

class FileStream final : public StreamLike {
 public:
  explicit FileStream(const fs::path& path) : file_(path, std::ios::binary), pos_(0) {}

  bool IsOpen() const { return file_.is_open(); }

  bool Seek(std::uint64_t absolute) override {
    if (!file_.is_open()) {
      return false;
    }
    file_.seekg(static_cast<std::streamoff>(absolute), std::ios::beg);
    if (!file_) {
      return false;
    }
    pos_ = absolute;
    return true;
  }

  std::size_t Read(void* dst, std::size_t size) override {
    if (!file_.is_open() || dst == nullptr || size == 0) {
      return 0;
    }
    file_.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(size));
    const std::size_t got = static_cast<std::size_t>(file_.gcount());
    pos_ += got;
    return got;
  }

  std::uint64_t Tell() const override { return pos_; }

 private:
  std::ifstream file_;
  std::uint64_t pos_;
};

class MemoryReadStream final : public StreamLike {
 public:
  explicit MemoryReadStream(std::vector<std::uint8_t> blob) : blob_(std::move(blob)), pos_(0) {}

  bool Seek(std::uint64_t absolute) override {
    if (absolute > blob_.size()) {
      return false;
    }
    pos_ = static_cast<std::size_t>(absolute);
    return true;
  }

  std::size_t Read(void* dst, std::size_t size) override {
    if (dst == nullptr || size == 0 || pos_ >= blob_.size()) {
      return 0;
    }
    const std::size_t remain = blob_.size() - pos_;
    const std::size_t n = (std::min)(size, remain);
    std::memcpy(dst, blob_.data() + pos_, n);
    pos_ += n;
    return n;
  }

  std::uint64_t Tell() const override { return static_cast<std::uint64_t>(pos_); }

 private:
  std::vector<std::uint8_t> blob_;
  std::size_t pos_;
};

// sub_14026FBA0-like: require exact length.
static bool ReadExact_Sub14026FBA0(StreamLike* stream, void* dst, std::size_t size) {
  if (stream == nullptr || dst == nullptr) {
    return false;
  }
  std::size_t done = 0;
  auto* out = static_cast<std::uint8_t*>(dst);
  while (done < size) {
    const std::size_t got = stream->Read(out + done, size - done);
    if (got == 0) {
      return false;
    }
    done += got;
  }
  return true;
}

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
  std::string folderPrefix_;
  std::unordered_map<std::uint16_t, std::uint32_t> rdx_;
  std::vector<RdbEntry> entries_;
  std::unordered_map<std::uint32_t, std::size_t> byKtid_;
};

// --------------------------- Mount Side Types --------------------------------

struct NodeSegment {
  std::uint32_t packedA = 0;
  std::uint32_t packedB = 0;
  std::uint32_t subId = 0;
};

struct RdbNode {
  std::uint32_t flags = 0;          // +0x28
  std::uint32_t segmentCount = 0;   // +0x30
  std::uint64_t nodeDataPtr = 0;    // +0x18
  std::vector<NodeSegment> segments;  // +0x38
};

// Runtime object passed as a7 in 0x14038B5F0.
struct RuntimeEntryState {
  std::uint32_t resourceHash = 0;      // used by hash logging path
  std::uint32_t entryType = 0;         // *(a7 + 8)
  std::int64_t mountedResource = 0;    // *(a7 + 16)
  std::uint32_t rangeBegin = 0;        // *(a7 + 24)
  std::uint32_t rangeEnd = 0;          // *(a7 + 28)
  std::uint32_t stageValueA = 0;       // *(a7 + 32)
  std::uint32_t stageValueB = 0;       // *(a7 + 36)
  std::uint32_t stateFlags = 0;        // *(a7 + 40)
  std::uint64_t notifier = 0;          // *(a7 + 48)
};

struct SegmentDesc {
  std::uint32_t formatAndCount = 0;
  std::uint32_t subId = 0;
  std::uint64_t refOrOffset = 0;
  std::uint64_t sizeOrAddr = 0;
  std::uint8_t* dataPtr = nullptr;
  std::uint32_t stateFlags = 0;
  std::uint32_t reserved = 0;
};

struct SegmentTableRaw {
  std::uint32_t count = 0;
  SegmentDesc* entries = nullptr;
  void* reserved = nullptr;
};

struct MountBuildContext {
  ArchiveIndex* archiveIndex = nullptr;  // v58[1]
  StreamLike* stream = nullptr;          // v58[2]
  std::uint64_t extraB = 0;              // v58[3]
  std::uint64_t extraA = 0;              // v58[4]
  std::uint64_t nodeDataPtr = 0;         // v58[5]
  void* preCreateTracker = nullptr;      // v53
};

struct CreateContext {
  void* ownerObj = nullptr;            // a4
  ArchiveIndex* archiveIndex = nullptr;  // a3 subfield
  std::uint32_t range2d[2] = {0, 0};
  RuntimeEntryState* runtime = nullptr;  // a7
  std::uint64_t callbackCtx = 0;         // a13
};

// a12 family in 0x14038B5F0.
struct DecoderStageSet {
  StreamLike* mode1 = nullptr;  // corresponds to *a12 path -> sub_1410D0EE0
  StreamLike* mode4 = nullptr;  // corresponds to a12[1] -> sub_1430D8D0C
  StreamLike* mode3 = nullptr;  // corresponds to a12[2] virtual +0x30
};

struct MountArgs {
  void* ownerA = nullptr;   // a4
  void* ownerB = nullptr;   // a5
  bool wrapStage2 = false;  // a6
  std::uint32_t syncMask = 0;  // a2
  std::uint64_t extraA = 0;    // a10
  std::uint64_t extraB = 0;    // a11
  DecoderStageSet* decoders = nullptr;  // a12
  std::uint64_t callbackCtx = 0;        // a13
  bool forceDecoderPath = false;        // a14
};

struct TypeHandler {
  virtual ~TypeHandler() = default;
  virtual std::uint32_t TypeId() const = 0;
  virtual std::int64_t CreateResource(const CreateContext& createCtx,
                                      const MountBuildContext& buildCtx,
                                      const SegmentTableRaw& segTable) = 0;
};

class RawBlobHandler final : public TypeHandler {
 public:
  explicit RawBlobHandler(std::uint32_t tid) : tid_(tid) {}

  std::uint32_t TypeId() const override { return tid_; }

  std::int64_t CreateResource(const CreateContext& createCtx,
                              const MountBuildContext& buildCtx,
                              const SegmentTableRaw& segTable) override {
    (void)createCtx;
    (void)buildCtx;
    (void)segTable;
    static std::int64_t fakeHandle = 1;
    return fakeHandle++;
  }

 private:
  std::uint32_t tid_;
};

struct ArchiveContext {
  fs::path packageRoot;
  ArchiveIndex index;
  std::unordered_map<std::uint32_t, std::unique_ptr<TypeHandler>> handlers;
  bool enablePreCreateTracker = false;  // mirrors conditional sub_14026F410 path
};

// ------------------------ Mount helper sub-functions -------------------------

static TypeHandler* TypeHandler_FindByTypeId_14038A594(ArchiveContext* ctx, std::uint32_t typeId) {
  if (ctx == nullptr) {
    return nullptr;
  }
  const auto it = ctx->handlers.find(typeId);
  if (it != ctx->handlers.end()) {
    return it->second.get();
  }
  auto owned = std::make_unique<RawBlobHandler>(typeId);
  TypeHandler* raw = owned.get();
  ctx->handlers[typeId] = std::move(owned);
  return raw;
}

static std::uint32_t Rdb_FindResHashByKtid_1413F13F0_Readable(const RuntimeEntryState* entry) {
  if (entry == nullptr) {
    return 0xFFFFFFFFu;
  }
  return entry->resourceHash;
}

static void sub_14038C388_CopyRange(std::uint32_t outRange[2], const std::uint32_t inRange[2]) {
  outRange[0] = inRange[0];
  outRange[1] = inRange[1];
}

static CreateContext sub_14038C344_BuildCreateContext(void* ownerObj,
                                                       ArchiveIndex* index,
                                                       const std::uint32_t range2d[2],
                                                       RuntimeEntryState* runtime,
                                                       std::uint64_t callbackCtx) {
  CreateContext c;
  c.ownerObj = ownerObj;
  c.archiveIndex = index;
  c.range2d[0] = range2d[0];
  c.range2d[1] = range2d[1];
  c.runtime = runtime;
  c.callbackCtx = callbackCtx;
  return c;
}

// IDA: sub_14038C1A4
// Build descriptor table in a caller-provided workspace.
static SegmentTableRaw Mount_BuildSegmentTable_14038C1A4(const RdbNode* node,
                                                         std::uint8_t* workspace,
                                                         std::size_t workspaceSize,
                                                         void* preCreateTracker) {
  static constexpr std::array<std::uint64_t, 16> kElemSize = {
      1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 16, 64, 8, 12, 8, 0,
  };

  SegmentTableRaw out{};
  if (node == nullptr || workspace == nullptr || workspaceSize < 24) {
    return out;
  }

  const std::size_t need = 24u + static_cast<std::size_t>(node->segmentCount) * sizeof(SegmentDesc);
  if (workspaceSize < need) {
    return out;
  }

  out.count = node->segmentCount;
  out.entries = reinterpret_cast<SegmentDesc*>(workspace + 24u);
  out.reserved = nullptr;

  std::uint8_t* dataCursor = workspace + need;
  std::size_t remain = workspaceSize - need;

  for (std::size_t i = 0; i < out.count; ++i) {
    const NodeSegment& src = (i < node->segments.size()) ? node->segments[i] : NodeSegment{};
    SegmentDesc& dst = out.entries[i];
    dst.formatAndCount = src.packedB | (src.packedA << 24);
    dst.subId = src.subId;
    dst.refOrOffset = reinterpret_cast<std::uint64_t>(preCreateTracker);
    dst.sizeOrAddr = 0;
    dst.dataPtr = dataCursor;

    const std::uint8_t fmt = static_cast<std::uint8_t>((dst.formatAndCount >> 24) & 0x0Fu);
    const std::uint32_t cnt = dst.formatAndCount & 0x00FFFFFFu;
    const std::size_t rawStep = static_cast<std::size_t>(kElemSize[fmt] * static_cast<std::uint64_t>(cnt));
    const std::size_t step = (std::min)(rawStep, remain);
    dst.sizeOrAddr = step;

    // Approximate state bit composition from sub_14038C1A4.
    dst.stateFlags = (preCreateTracker != nullptr) ? 0x02u : 0x01u;
    if (step == 0) {
      dst.stateFlags |= 0x04u;
    } else {
      dst.stateFlags |= 0x08u;
    }
    dst.reserved = 0;

    dataCursor += step;
    remain -= step;
  }

  return out;
}

static StreamLike* sub_140C6F5D8_DefaultWrap(StreamLike* in, std::uint64_t /*nodeCtx*/) {
  // Real function rebuilds an adapter object. For readable flow we keep pass-through.
  return in;
}

static StreamLike* sub_1430CEAE8_Stage2Wrap(StreamLike* in, std::uint64_t /*nodeCtx*/) {
  // Real function adds one more wrapper layer.
  return in;
}

enum class DecoderSelectResult : std::uint8_t {
  NotApplicable,
  Success,
  Failed
};

static DecoderSelectResult TrySelectDecoderForMode(std::uint32_t compressionMode,
                                                   DecoderStageSet* decoders,
                                                   StreamLike** ioStream) {
  if (decoders == nullptr || ioStream == nullptr) {
    return DecoderSelectResult::NotApplicable;
  }

  switch (compressionMode) {
    case 1:
      if (decoders->mode1 == nullptr) return DecoderSelectResult::Failed;
      *ioStream = decoders->mode1;
      return DecoderSelectResult::Success;
    case 3:
      if (decoders->mode3 == nullptr) return DecoderSelectResult::Failed;
      *ioStream = decoders->mode3;
      return DecoderSelectResult::Success;
    case 4:
      if (decoders->mode4 == nullptr) return DecoderSelectResult::Failed;
      *ioStream = decoders->mode4;
      return DecoderSelectResult::Success;
    default:
      return DecoderSelectResult::NotApplicable;
  }
}

static void sub_14007EBD0_ClearSync(volatile std::int32_t* /*syncWord*/, std::uint32_t /*mask*/) {
  // Placeholder for bit clear helper.
}

static void sub_14107DDA0_LogMountError(const char* /*name*/, std::uint32_t /*hash*/) {
  // Placeholder for game logger call.
}

// -------------------------- fdata read side helpers --------------------------

struct IoDescriptor {
  fs::path containerPath;
  std::uint64_t blockOffset = 0;
  std::uint64_t blockSpan = 0;
  std::uint64_t headerSkip = 0x38;  // observed in Rdb_ReadPayloadThenMount_14062B724
  std::uint64_t payloadOffset = 0;
  std::uint64_t payloadSize = 0;
};

// Reconstructed from:
// - Rdb_ResolveContainerAndBlock_14062C4F8
// - Rdb_FormatContainerPath_14062C918
// - EntryStorageModeFromFlags_14062C7B4
// - Rdb_QueryExternalFileOffset_14062C414
static bool ResolveIoDescriptorFromRdb(const ArchiveContext& ctx, std::uint32_t fileKtid, IoDescriptor* out) {
  if (out == nullptr) {
    return false;
  }

  const RdbEntry* r = ctx.index.FindByKtid(fileKtid);
  if (r == nullptr) {
    return false;
  }

  out->containerPath = ctx.packageRoot / r->location.containerPath;
  out->blockOffset = r->location.offset;
  out->blockSpan = r->location.sizeInContainer;

  const std::uint64_t delta = (r->entrySize > r->dataSize) ? (r->entrySize - r->dataSize) : 0;
  out->headerSkip = (delta != 0) ? delta : 0x38;

  if (out->blockSpan > out->headerSkip) {
    out->payloadOffset = out->blockOffset + out->headerSkip;
    out->payloadSize = out->blockSpan - out->headerSkip;
  } else {
    out->payloadOffset = out->blockOffset;
    out->payloadSize = out->blockSpan;
  }

  return true;
}

// Reconstructed from:
// - Vfs_OpenContainerStream_14062C304
// - FsHandle_Seek_1400D529C / FsHandle_SeekInternal_1400D51B8
// - Stream_ReadExactAndBindBuffer_14026FBA0
// - VfsStream_Read_14062EC48
static std::vector<std::uint8_t> ReadPayloadFromContainer(const IoDescriptor& io) {
  std::vector<std::uint8_t> payload;

  FileStream fs(io.containerPath);
  if (!fs.IsOpen() || !fs.Seek(io.payloadOffset) || io.payloadSize == 0) {
    return payload;
  }

  payload.resize(static_cast<std::size_t>(io.payloadSize));
  if (!ReadExact_Sub14026FBA0(&fs, payload.data(), payload.size())) {
    payload.clear();
  }
  return payload;
}

// ------------------------------ Mount itself ---------------------------------

// IDA-like flow for 0x14038B5F0.
static bool VFS_MountResource_14038B5F0_IdaFlow(volatile std::int32_t* syncWord,
                                                ArchiveContext* archiveCtx,
                                                RuntimeEntryState* runtime,
                                                const RdbNode* node,
                                                StreamLike* a9Stream,
                                                const MountArgs& args) {
  // Step 0: guard checks (not explicit in IDA, added for readable safety).
  if (archiveCtx == nullptr || runtime == nullptr || node == nullptr || a9Stream == nullptr) {
    return false;
  }

  // Step 1: decode compression mode from node flags.
  // IDA: v19 = (*(_DWORD *)(a8 + 40) >> 20) & 0x3F
  const std::uint32_t compressionMode = (node->flags >> 20) & 0x3Fu;

  // Step 2: initialize active stream object (v20 in IDA).
  // mode == 0 path uses sub_140C6F5D8 wrapper; otherwise direct stream.
  StreamLike* activeStream = a9Stream;
  if (compressionMode == 0) {
    activeStream = sub_140C6F5D8_DefaultWrap(a9Stream, node->nodeDataPtr);
  }

  // Step 3: optional stage-2 stream wrap.
  // IDA: if (a6) sub_1430CEAE8(...)
  if (args.wrapStage2) {
    activeStream = sub_1430CEAE8_Stage2Wrap(activeStream, node->nodeDataPtr);
  }

  // Step 4: optional decoder set branch (LABEL_11 .. LABEL_18 area).
  // Only mode 1/3/4 consumes a12 entries; failure path returns immediately.
  // mode 1 -> sub_1410D0EE0
  // mode 3 -> a12[2] virtual call
  // mode 4 -> sub_1430D8D0C
  if (args.decoders != nullptr) {
    const DecoderSelectResult dr = TrySelectDecoderForMode(compressionMode, args.decoders, &activeStream);
    if (dr == DecoderSelectResult::Failed) {
      // IDA failure branch clears sync bits (a2) and returns 0.
      if (args.syncMask != 0) {
        sub_14007EBD0_ClearSync(syncWord, args.syncMask);
      }
      return false;
    }
    (void)args.forceDecoderPath;
  }

  // Step 5: optional pre-create tracker (v53 in IDA).
  // IDA gate is driven by archive flags around (a3 + 508) and sub_14026F410.
  void* preCreateTracker = archiveCtx->enablePreCreateTracker ? reinterpret_cast<void*>(0x1) : nullptr;

  // Step 6: build v58-like creation bundle.
  // IDA layout:
  //   v58[1]=a3, v58[2]=activeStream, v58[3]=a11, v58[4]=a10, v58[5]=*(a8+24)
  MountBuildContext buildCtx{};
  buildCtx.archiveIndex = &archiveCtx->index;
  buildCtx.stream = activeStream;
  buildCtx.extraB = args.extraB;
  buildCtx.extraA = args.extraA;
  buildCtx.nodeDataPtr = node->nodeDataPtr;
  buildCtx.preCreateTracker = preCreateTracker;

  // Step 7: allocate temporary segment workspace.
  // IDA: size = 24 + 32 * *(a8 + 48), local alloca for <= 0x4000 else allocator.
  const std::size_t workspaceNeed = 24u + (static_cast<std::size_t>(node->segmentCount) * 32u);
  std::array<std::uint8_t, 0x4000> localWork{};
  std::unique_ptr<std::uint8_t[]> heapWork;
  std::uint8_t* workPtr = nullptr;
  std::size_t workSize = 0;

  if (workspaceNeed <= localWork.size()) {
    workPtr = localWork.data();
    workSize = workspaceNeed;
  } else {
    heapWork = std::make_unique<std::uint8_t[]>(workspaceNeed);
    workPtr = heapWork.get();
    workSize = workspaceNeed;
  }

  // Step 8: move/reset runtime state fields on a7.
  // Mirrors:
  //   *(a7+24/28) <= old *(a7+32/36)
  //   *(a7+32)=0, *(a7+40)=0, exchange *(a7+48) with 0
  const std::uint32_t oldRangeBegin = runtime->stageValueA;
  const std::uint32_t oldRangeEnd = runtime->stageValueB;
  runtime->stageValueA = 0;
  runtime->rangeBegin = oldRangeBegin;
  runtime->rangeEnd = oldRangeEnd;
  runtime->stateFlags = 0;
  const std::uint64_t oldNotifier = std::exchange(runtime->notifier, 0ULL);

  // Step 9: build range copy and create context.
  std::uint32_t rangeSrc[2] = {runtime->rangeBegin, runtime->rangeEnd};
  std::uint32_t rangeCopy[2] = {0, 0};
  sub_14038C388_CopyRange(rangeCopy, rangeSrc);

  CreateContext createCtx = sub_14038C344_BuildCreateContext(
      args.ownerA, &archiveCtx->index, rangeCopy, runtime, args.callbackCtx);

  // Step 10: lookup handler by entry type (sub_14038A594(a3, *(a7+8))).
  TypeHandler* handler = TypeHandler_FindByTypeId_14038A594(archiveCtx, runtime->entryType);
  if (handler == nullptr) {
    // handler missing -> failure path
    if (args.syncMask != 0) {
      sub_14007EBD0_ClearSync(syncWord, args.syncMask);
    }
    return false;
  }

  // Step 11: build segment table (sub_14038C1A4) and create resource via handler->vft[0xB0].
  SegmentTableRaw segTable = Mount_BuildSegmentTable_14038C1A4(node, workPtr, workSize, preCreateTracker);
  const std::int64_t created = handler->CreateResource(createCtx, buildCtx, segTable);
  runtime->mountedResource = created;

  // Step 12: common sync clear path.
  if (args.syncMask != 0) {
    sub_14007EBD0_ClearSync(syncWord, args.syncMask);
  }

  // Step 13: success branch (IDA also has notifier callback integration here).
  if (created != 0) {
    (void)oldNotifier;
    (void)args.ownerB;
    return true;
  }

  // Step 14: failure logging branch.
  // IDA: resolve hash via Rdb_FindResHashByKtid_1413F13F0 and log
  // "can't create resource Name[%s] Hash[0x%08x]".
  const std::uint32_t hash = Rdb_FindResHashByKtid_1413F13F0_Readable(runtime);
  sub_14107DDA0_LogMountError("Unknown", hash);
  return false;
}

// Exact-shape readable wrapper for IDA signature:
// char __fastcall VFS_MountResource_14038B5F0(
//   volatile int32_t* a1, uint32_t a2, ArchiveContext* a3, void* a4, void* a5,
//   char a6, RuntimeEntryState* a7, RdbNode* a8, StreamLike* a9,
//   uint64_t a10, uint64_t a11, DecoderStageSet* a12, uint64_t a13, char a14)
static bool VFS_MountResource_14038B5F0_ExactSig(volatile std::int32_t* a1,
                                                 std::uint32_t a2,
                                                 ArchiveContext* a3,
                                                 void* a4,
                                                 void* a5,
                                                 bool a6,
                                                 RuntimeEntryState* a7,
                                                 const RdbNode* a8,
                                                 StreamLike* a9,
                                                 std::uint64_t a10,
                                                 std::uint64_t a11,
                                                 DecoderStageSet* a12,
                                                 std::uint64_t a13,
                                                 bool a14) {
  // Adapter: keep the exact IDA parameter order at call sites, then map to MountArgs.
  MountArgs args;
  args.ownerA = a4;
  args.ownerB = a5;
  args.wrapStage2 = a6;
  args.syncMask = a2;
  args.extraA = a10;
  args.extraB = a11;
  args.decoders = a12;
  args.callbackCtx = a13;
  args.forceDecoderPath = a14;
  return VFS_MountResource_14038B5F0_IdaFlow(a1, a3, a7, a8, a9, args);
}

// -------------------------- End-to-end helper API ---------------------------

// Equivalent high-level flow:
// Rdb_ReadPayloadThenMount_14062B724
//   -> Stream_ReadExactAndBindBuffer_14026FBA0
//   -> VFS_MountResource_14038B5F0
bool LoadFdataAndMountResource_Readable(ArchiveContext* archiveCtx,
                                        RuntimeEntryState* entry,
                                        const RdbNode* node,
                                        const MountArgs& args) {
  // Stage A (Rdb_ReadPayloadThenMount_14062B724): resolve container + block info.
  if (archiveCtx == nullptr || entry == nullptr || node == nullptr) {
    return false;
  }

  IoDescriptor io;
  if (!ResolveIoDescriptorFromRdb(*archiveCtx, entry->resourceHash, &io)) {
    return false;
  }

  // Stage B: open container, seek payload offset, read exact payload size.
  std::vector<std::uint8_t> payload = ReadPayloadFromContainer(io);
  if (payload.empty()) {
    return false;
  }

  // Stage C: pass payload stream into 0x14038B5F0-equivalent flow.
  MemoryReadStream raw(std::move(payload));
  raw.Seek(0);
  return VFS_MountResource_14038B5F0_ExactSig(
      nullptr,              // a1: sync word
      args.syncMask,        // a2
      archiveCtx,           // a3
      args.ownerA,          // a4
      args.ownerB,          // a5
      args.wrapStage2,      // a6
      entry,                // a7
      node,                 // a8
      &raw,                 // a9
      args.extraA,          // a10
      args.extraB,          // a11
      args.decoders,        // a12
      args.callbackCtx,     // a13
      args.forceDecoderPath // a14
  );
}

}  // namespace nioh3::vfs

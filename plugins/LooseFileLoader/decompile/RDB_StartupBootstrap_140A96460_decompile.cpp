// Readable reconstruction for startup-time RDB/RDX bootstrap flow.
//
// Main path covered in this file:
// 1) Boot enumerates "system.rdb" / "root.rdb"
// 2) Register each RDB slot (+ parse header) and load sibling *.rdx mapping
// 3) Build runtime resource objects (shell only, no fdata deserialize yet)
// 4) Runtime later performs on-demand mount/deserialize via MountResource_1405E5038
//
// Function mapping to IDA symbols:
// - sub_140A96460:  Boot_LoadRdbList_140A96460
// - sub_140A96F14:  QueueRdbLoad_140A96F14
// - sub_140A96F68:  RegisterRdbSlot_140A96F68
// - sub_140A9717C:  ReadRdbFileInfoHeader_140A9717C
// - sub_140C6F26C:  LoadRdxSidecar_140C6F26C
// - sub_1415D4090:  FinalizeRdbBootstrap_1415D4090
// - sub_140A95688:  StartBuildResourceObjects_140A95688
// - sub_140215F44:  BuildResourceObjectsFromRdb_140215F44
// - sub_1408C4CBC:  BuildFileKtidToResFileId_1408C4CBC
// - sub_1408C556C:  RadixSortFileKtid_1408C556C
// - sub_140773EAC:  GetResFileIdByFileKtid_140773EAC
// - sub_1409CF148:  GetRdbRuntimeEntryDesc_1409CF148

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nioh3::vfs {

namespace fs = std::filesystem;

// Runtime descriptor used by mount path (same role as the in-memory RIS entry).
struct RdbRuntimeEntryDesc {
  std::uint32_t stateFlags = 0;      // +0x00 runtime state bits (load/create/pending)
  std::uint32_t refFlags = 0;        // +0x04 runtime status flags
  std::uint32_t typeInfoKtid = 0;    // +0x08 resource type id/hash
  std::uint32_t reserved0C = 0;      // +0x0C
  std::uint64_t objectPtr = 0;       // +0x10 created runtime object (shell), may be null
  std::uint64_t paramDataBlock = 0;  // +0x18
  std::uint32_t rangeBegin = 0;      // +0x20
  std::uint32_t rangeSize = 0;       // +0x24
  std::uint32_t fileKtid = 0;        // +0x28
  std::uint32_t reserved24 = 0;      // +0x2C
  std::uint32_t flags = 0;           // +0x30 RDB/RIS flags (compression mode in high bits)
  std::uint32_t paramCount = 0;      // +0x34
};

// 24-byte file-info header checked by sub_140A9717C.
struct RdbFileInfoHeader24 {
  std::uint32_t magic0 = 0;          // +0x00 "_DRK"
  std::uint32_t version = 0;         // +0x04 "0000"
  std::uint32_t bodyBytes = 0;       // +0x08
  std::uint32_t systemId = 0;        // +0x0C
  std::uint32_t sectionCount = 0;    // +0x10
  std::uint32_t unknown14 = 0;       // +0x14
};

// 56-byte resource-info header consumed by sub_140215F44 loop.
struct ResourceInfoHeader56 {
  std::uint32_t magic0 = 0;          // +0x00 "IDRK"
  std::uint32_t version = 0;         // +0x04 "0000"
  std::uint64_t allBlockSize = 0;    // +0x08
  std::uint64_t compressedSize = 0;  // +0x10
  std::uint64_t fileSize = 0;        // +0x18
  std::uint32_t paramDataSize = 0;   // +0x20
  std::uint32_t fileKtid = 0;        // +0x24 (hashName)
  std::uint32_t typeInfoKtid = 0;    // +0x28 (hashType)
  std::uint32_t flags = 0;           // +0x2C
  std::uint32_t resourceId = 0;      // +0x30
  std::uint32_t paramCount = 0;      // +0x34 (paramHeaderCount)
};

static_assert(sizeof(RdbRuntimeEntryDesc) == 0x38);
static_assert(sizeof(RdbFileInfoHeader24) == 0x18);
static_assert(sizeof(ResourceInfoHeader56) == 0x38);

struct LoadedRdbSlot {
  fs::path rdbPath;
  std::uint32_t sectionCount = 0;
};

struct RuntimeBootstrapState {
  std::uint64_t bootFlags496 = 0;  // contains 0x800000 / 0x200000 gates
  std::uint32_t dbFlags200 = 0;    // contains 0x40000000 "RDB ready" gate

  std::vector<LoadedRdbSlot> loadedRdbs;
  std::unordered_map<std::uint16_t, std::uint32_t> rdxMapByFdataId; // FDataId -> FileId

  // Built during sub_140215F44 first pass + sub_1408C4CBC build step.
  std::vector<std::uint32_t> sortedFileKtidKeys;
  std::unordered_map<std::uint32_t, std::uint32_t> fileKtidToResFileId;

  // Flat runtime table queried by sub_1409CF148.
  std::vector<RdbRuntimeEntryDesc> runtimeEntries;
};

struct PendingResourceShellInit {
  std::uint32_t sectionIndex = 0;
  ResourceInfoHeader56 header{};
};

static constexpr std::uint32_t kInvalidResFileId = std::numeric_limits<std::uint32_t>::max();
static constexpr std::uint32_t kDrkMagic = 0x5F44524B;   // "_DRK"
static constexpr std::uint32_t kIdrkMagic = 0x4B524449;  // "IDRK"
static constexpr std::uint32_t kAscii0000 = 0x30303030;  // "0000"

static bool ReadRdbFileInfoHeader_140A9717C(std::istream& in, RdbFileInfoHeader24* out) {
  if (out == nullptr) {
    return false;
  }
  RdbFileInfoHeader24 h{};
  in.read(reinterpret_cast<char*>(&h), sizeof(h));
  if (!in) {
    return false;
  }
  if (h.magic0 != kDrkMagic) {
    return false;
  }
  if (h.version != kAscii0000) {
    return false;
  }
  if (h.bodyBytes < sizeof(RdbFileInfoHeader24)) {
    return false;
  }
  *out = h;
  return true;
}

// rdx sidecar loader used by resource database provider (sub_140C6F26C path).
static bool LoadRdxSidecar_140C6F26C(RuntimeBootstrapState* state, const fs::path& rdbPath) {
  if (state == nullptr) {
    return false;
  }

  fs::path rdxPath = rdbPath;
  rdxPath.replace_extension(".rdx");

  std::ifstream in(rdxPath, std::ios::binary);
  if (!in) {
    return false;
  }

  struct RdxEntry {
    std::uint16_t index;
    std::uint16_t marker;
    std::uint32_t fileId;
  };

  while (true) {
    RdxEntry e{};
    in.read(reinterpret_cast<char*>(&e), sizeof(e));
    if (!in) {
      break;
    }
    state->rdxMapByFdataId[e.index] = e.fileId;
  }
  return true;
}

static bool RegisterRdbSlot_140A96F68(RuntimeBootstrapState* state, const fs::path& rdbPath) {
  if (state == nullptr) {
    return false;
  }
  if ((state->dbFlags200 & 0x40000000u) == 0) {
    return false;
  }

  std::ifstream in(rdbPath, std::ios::binary);
  if (!in) {
    return false;
  }

  RdbFileInfoHeader24 hdr{};
  if (!ReadRdbFileInfoHeader_140A9717C(in, &hdr)) {
    return false;
  }

  state->loadedRdbs.push_back(LoadedRdbSlot{
      .rdbPath = rdbPath,
      .sectionCount = hdr.sectionCount,
  });

  // Root/system DB startup path loads sibling .rdx as container id mapping.
  LoadRdxSidecar_140C6F26C(state, rdbPath);
  return true;
}

static bool QueueRdbLoad_140A96F14(RuntimeBootstrapState* state, const fs::path& rdbPath) {
  if (state == nullptr) {
    return false;
  }
  if ((state->bootFlags496 & 0x800000ull) == 0) {
    return false;
  }
  if ((state->bootFlags496 & 0x200000ull) != 0) {
    return false;
  }
  return RegisterRdbSlot_140A96F68(state, rdbPath);
}

static std::uint32_t GetResFileIdByFileKtid_140773EAC(const RuntimeBootstrapState& state, std::uint32_t fileKtid) {
  const auto it = state.fileKtidToResFileId.find(fileKtid);
  if (it == state.fileKtidToResFileId.end()) {
    return kInvalidResFileId;
  }
  return it->second;
}

static RdbRuntimeEntryDesc* GetRdbRuntimeEntryDesc_1409CF148(RuntimeBootstrapState* state, std::uint32_t resFileId) {
  if (state == nullptr || resFileId >= state->runtimeEntries.size()) {
    return nullptr;
  }
  return &state->runtimeEntries[resFileId];
}

static void InitializeRuntimeShell_14021655F(
    RdbRuntimeEntryDesc* dst,
    const ResourceInfoHeader56& src,
    std::uint32_t sectionIndex) {
  if (dst == nullptr) {
    return;
  }

  // Mirrors the shell-init block in sub_140215F44 (second pass, v54==1).
  const std::uint32_t compressionClass = (src.flags >> 20) & 0x3F;
  std::uint32_t stateClass = 0;
  if (compressionClass == 1) {
    stateClass = 0x10000000u;
  } else if (compressionClass == 2) {
    stateClass = 0x20000000u;
  }

  dst->stateFlags = stateClass | ((sectionIndex & 0xFu) << 24);
  dst->refFlags = 1;
  dst->typeInfoKtid = src.typeInfoKtid;
  dst->objectPtr = 0;
  dst->paramDataBlock = 0;
  dst->rangeBegin = 0;
  dst->rangeSize = 8;
  dst->fileKtid = src.fileKtid;
  dst->flags = src.flags;
  dst->paramCount = src.paramCount;
}

static bool ValidateResourceInfoHeader56_140216D9C(const ResourceInfoHeader56& h) {
  if (h.magic0 != kIdrkMagic || h.version != kAscii0000) {
    return false;
  }
  const std::uint64_t expected = h.compressedSize + static_cast<std::uint64_t>(12u * h.paramCount) + h.paramDataSize + 56u;
  if (h.allBlockSize != expected) {
    return false;
  }
  return true;
}

// sub_1408C556C: 4-pass radix sort on 32-bit fileKtid.
static void RadixSortFileKtid_1408C556C(std::vector<std::uint32_t>* keys) {
  if (keys == nullptr || keys->empty()) {
    return;
  }

  std::vector<std::uint32_t> scratch(keys->size());
  std::vector<std::uint32_t>* src = keys;
  std::vector<std::uint32_t>* dst = &scratch;

  for (int pass = 0; pass < 4; ++pass) {
    std::array<std::size_t, 256> count{};
    const int shift = pass * 8;

    for (std::uint32_t v : *src) {
      ++count[(v >> shift) & 0xFFu];
    }

    std::array<std::size_t, 256> prefix{};
    std::size_t running = 0;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
      prefix[i] = running;
      running += count[i];
    }

    for (std::uint32_t v : *src) {
      const std::size_t b = (v >> shift) & 0xFFu;
      (*dst)[prefix[b]++] = v;
    }

    std::swap(src, dst);
  }

  if (src != keys) {
    *keys = *src;
  }
}

// sub_1408C4CBC: build fileKtid -> resFileId query structure.
// Game uses compact multi-level table; readable reconstruction keeps sorted keys + direct map.
static void BuildFileKtidToResFileId_1408C4CBC(
    RuntimeBootstrapState* state,
    const std::vector<std::uint32_t>& collectedFileKtid) {
  if (state == nullptr) {
    return;
  }

  state->fileKtidToResFileId.clear();
  state->sortedFileKtidKeys = collectedFileKtid;
  RadixSortFileKtid_1408C556C(&state->sortedFileKtidKeys);

  std::uint32_t nextResFileId = 0;
  for (std::uint32_t fileKtid : state->sortedFileKtidKeys) {
    if (state->fileKtidToResFileId.find(fileKtid) != state->fileKtidToResFileId.end()) {
      continue;
    }
    state->fileKtidToResFileId.emplace(fileKtid, nextResFileId++);
  }
}

// Core startup builder: creates runtime resource shells from loaded RDB info.
static bool BuildResourceObjectsFromRdb_140215F44(RuntimeBootstrapState* state) {
  if (state == nullptr) {
    return false;
  }
  if ((state->dbFlags200 & 0x40000000u) == 0) {
    return false;
  }

  // First pass in sub_140215F44:
  // read all RIS headers and collect fileKtid list, then call sub_1408C4CBC to build search index.
  std::vector<PendingResourceShellInit> pending;
  std::vector<std::uint32_t> collectedFileKtid;

  std::uint32_t sectionIndex = 0;
  for (const LoadedRdbSlot& slot : state->loadedRdbs) {
    std::ifstream in(slot.rdbPath, std::ios::binary);
    if (!in) {
      return false;
    }

    // Real game code walks section descriptors and creates a stream per section.
    // For readable reconstruction we scan the file and consume each 56-byte RIS header.
    std::vector<char> blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (std::size_t i = 0; i + sizeof(ResourceInfoHeader56) <= blob.size(); ++i) {
      if (*reinterpret_cast<const std::uint32_t*>(blob.data() + i) != kIdrkMagic) {
        continue;
      }
      ResourceInfoHeader56 h{};
      std::memcpy(&h, blob.data() + i, sizeof(h));
      if (!ValidateResourceInfoHeader56_140216D9C(h)) {
        continue;
      }

      pending.push_back(PendingResourceShellInit{
          .sectionIndex = sectionIndex,
          .header = h,
      });
      collectedFileKtid.push_back(h.fileKtid);
    }
    ++sectionIndex;
  }

  BuildFileKtidToResFileId_1408C4CBC(state, collectedFileKtid);
  if (state->runtimeEntries.size() < state->fileKtidToResFileId.size()) {
    state->runtimeEntries.resize(state->fileKtidToResFileId.size());
  }

  // Second pass in sub_140215F44:
  // fileKtid -> resFileId (sub_140773EAC), then resFileId -> runtimeEntry (sub_1409CF148), then initialize shell.
  for (const PendingResourceShellInit& item : pending) {
    const std::uint32_t resFileId = GetResFileIdByFileKtid_140773EAC(*state, item.header.fileKtid);
    if (resFileId == kInvalidResFileId) {
      continue;
    }
    RdbRuntimeEntryDesc* runtime = GetRdbRuntimeEntryDesc_1409CF148(state, resFileId);
    if (runtime == nullptr) {
      continue;
    }
    InitializeRuntimeShell_14021655F(runtime, item.header, item.sectionIndex);
  }

  // Startup stops at shell creation.
  // Actual payload read + type deserialize happens later via:
  // sub_1405E6798 -> sub_1405E794C -> MountResource_1405E5038.
  return true;
}

static bool FinalizeRdbBootstrap_1415D4090(RuntimeBootstrapState* state) {
  if (state == nullptr) {
    return false;
  }
  if ((state->bootFlags496 & 0x800000ull) == 0) {
    return false;
  }
  if ((state->bootFlags496 & 0x200000ull) != 0) {
    return false;
  }
  return BuildResourceObjectsFromRdb_140215F44(state);
}

static bool StartBuildResourceObjects_140A95688(RuntimeBootstrapState* state) {
  if (state == nullptr) {
    return false;
  }
  if ((state->bootFlags496 & 0x800000ull) == 0) {
    return false;
  }
  if ((state->bootFlags496 & 0x200000ull) != 0) {
    return false;
  }

  if (!BuildResourceObjectsFromRdb_140215F44(state)) {
    return false;
  }

  // Clear "need build" bit on success (mirrors sub_140A95688 tail).
  state->bootFlags496 &= ~0x800000ull;
  return true;
}

bool Boot_LoadRdbList_140A96460(RuntimeBootstrapState* state, const fs::path& packageRoot) {
  if (state == nullptr) {
    return false;
  }

  // Mirrors off_143D73780 table in IDA.
  static constexpr std::array<const char*, 2> kBootRdbList_143D73780 = {
      "system.rdb",
      "root.rdb",
  };

  for (const char* name : kBootRdbList_143D73780) {
    if (!QueueRdbLoad_140A96F14(state, packageRoot / name)) {
      return false;
    }
  }

  return FinalizeRdbBootstrap_1415D4090(state);
}

}  // namespace nioh3::vfs

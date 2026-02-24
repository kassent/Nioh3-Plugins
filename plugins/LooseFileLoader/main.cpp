#define NOMINMAX
#include "Relocation.h"
#include <Windows.h>
#include <cstdint>
#include <PluginAPI.h>
#include <LogUtils.h>
#include <FileUtils.h>
#include <HookUtils.h>
#include <BranchTrampoline.h>
#include <GameType.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <limits>
#include <filesystem>
#include <memory>
#include "binary_io/binary_io.hpp"

namespace fs = std::filesystem;
#define PLUGIN_NAME "LooseFileLoader"
#define PLUGIN_VERSION_MAJOR 1
#define PLUGIN_VERSION_MINOR 0
#define PLUGIN_VERSION_PATCH 0


// #define _DEBUG

namespace {

#pragma pack(push, 1)
	struct RdbRuntimeEntryDesc {
		char magic[4];        // +0x00, "IDRK" (0x14062BBF8 初始化 + 0x140270A44读取)
		char version[4];      // +0x04
		uint64_t sizeInContainer;    // +0x08, allBlockSize in rdata
		uint64_t compressedSize; // +0x10
		uint64_t fileSize;     // +0x18 uncompressedSize in rdata
		uint32_t fileKtid;     // +0x20
		uint32_t typeInfoKtid; // +0x24, 0x14038C1A4 用于 TypeHandler 查找
		uint32_t flags;        // +0x28, 0x14038B5F0: (flags>>20)&0x3F 压缩模式
		uint32_t f2C;          // +0x2C
		uint32_t paramCount; // +0x30, 0x14038C1A4 使用
		uint32_t f34;          // +0x34, 当前路径未稳定使用
		void* paramDataBlock;  // +0x38

		std::string GetMagic() const {
			return std::string(magic, 4);
		}

		std::string GetVersion() const {
			return std::string(version, 4);
		}
	};
	static_assert(sizeof(RdbRuntimeEntryDesc) == 0x40);
#pragma pack(pop)
	// cmake --build build --config Release --target LooseFileLoader


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
		virtual std::uint64_t ReadByte(std::uint8_t* a_outByte) = 0;

		// +0x20, FileReader_Read_1405EABD8
		// Read a_size bytes into (a_dst + a_dstOffset), return actual bytes read.
		virtual std::uint64_t Read(void* a_dst, std::uint64_t a_dstOffset, std::uint64_t a_size) = 0;

		// +0x28, FileReader_QueryCapability_142E74714
		// FileReader implementation returns 0 (unsupported/no-op capability).
		virtual std::uint64_t QueryCapability() = 0;
	};


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
			if (m_stream.is_open()) {
				Close();
			}
			m_filePath = std::move(a_filePath);

			if (m_filePath.empty() || !fs::exists(m_filePath) || !fs::is_regular_file(m_filePath)) {
				return false;
			}

			try {
				m_fileSize = fs::file_size(m_filePath);
				m_stream.open(m_filePath);
				//m_stream.seek_absolute(0);
			}
			catch (...) {
				Close();
				return false;
			}
			return m_stream.is_open();
		}

		void Close() override {
			if (m_stream.is_open()) {
				m_stream.close();
			}
		}

		std::int64_t Skip(std::int64_t a_deltaBytes) override {
			if (!m_stream.is_open()) {
				return 0;
			}

			const std::int64_t cur = static_cast<std::int64_t>(m_stream.tell());
			const std::int64_t end = static_cast<std::int64_t>(m_fileSize);
			const std::int64_t target = std::clamp(cur + a_deltaBytes, std::int64_t{ 0 }, end);

			m_stream.seek_absolute(static_cast<binary_io::streamoff>(target));
			return target - cur;
		}

		std::uint64_t ReadByte(std::uint8_t* a_outByte) override {
			if (a_outByte == nullptr) {
				return 0;
			}
			return Read(a_outByte, 0, 1);
		}

		std::uint64_t Read(void* a_dst, std::uint64_t a_dstOffset, std::uint64_t a_size) override {
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
			}
			catch (const binary_io::buffer_exhausted&) {
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


	class IBaseGameAssetHandler {
	public:
		virtual void Unk00() = 0;
		virtual void Unk08() = 0;
		virtual void Unk10() = 0;
		virtual const char*& GetTypeName(const char*& a_typeNameOut) const = 0;
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
		virtual void* Deserialize(void* a_param1, IAssetStreamReader* a_reader, void* a_param3) = 0; // B0

		uintptr_t GetVtableAddr() const {
			return *reinterpret_cast<const uintptr_t*>(this) - RelocationManager::s_baseAddr + 0x140000000ull;
		}

		std::string GetTypeName() const {
			const char* typeNamePtr = nullptr;
			GetTypeName(typeNamePtr);
			return typeNamePtr ? typeNamePtr : "Unknown";
		}
	};


	fs::path g_gameRootDir;
	fs::path g_pluginsDir;
	std::unordered_map<std::uint32_t, fs::path> g_modAssetOverrides;

	struct ModAssetCandidate {
		std::uint32_t fileHash = 0;
		fs::path filePath{};
		bool fromModsRoot = false;
		std::string parentSortKey{};
		std::string fileSortKey{};
	};

	std::string ToLowerAscii(std::string a_text) {
		std::transform(a_text.begin(), a_text.end(), a_text.begin(),
			[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		return a_text;
	}

	bool TryParseAssetHashFromFileName(const fs::path& a_path, std::uint32_t& a_outHash) {
		std::string fileName = a_path.filename().string();
		const auto dotPos = fileName.find('.');
		if (dotPos != std::string::npos) {
			fileName = fileName.substr(0, dotPos);
		}

		if (fileName.empty()) {
			return false;
		}

		std::string hexText;
		if (fileName.size() == 10 && fileName[0] == '0' && (fileName[1] == 'x' || fileName[1] == 'X')) {
			hexText = fileName.substr(2);
		}
		else if (fileName.size() == 8) {
			hexText = fileName;
		}
		else {
			return false;
		}

		if (!std::all_of(hexText.begin(), hexText.end(),
			[](unsigned char ch) { return std::isxdigit(ch) != 0; })) {
			return false;
		}

		try {
			const auto value = std::stoull(hexText, nullptr, 16);
			if (value > std::numeric_limits<std::uint32_t>::max()) {
				return false;
			}
			a_outHash = static_cast<std::uint32_t>(value);
			return true;
		}
		catch (...) {
			return false;
		}
	}

	void CollectModAssetCandidates(const fs::path& a_dir, bool a_fromModsRoot, const std::string& a_parentSortKey, std::vector<ModAssetCandidate>& a_outCandidates) {
		std::error_code ec;
		if (!fs::exists(a_dir, ec) || !fs::is_directory(a_dir, ec)) {
			return;
		}

		for (const auto& entry : fs::directory_iterator(a_dir, fs::directory_options::skip_permission_denied, ec)) {
			if (ec) {
				_MESSAGE("Failed to iterate directory: %s", a_dir.string().c_str());
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
			candidate.fromModsRoot = a_fromModsRoot;
			candidate.parentSortKey = a_parentSortKey;
			candidate.fileSortKey = ToLowerAscii(entry.path().filename().string());
			a_outCandidates.emplace_back(std::move(candidate));
		}
	}

	void BuildModAssetOverrideIndex() {
		g_modAssetOverrides.clear();

		const fs::path modsDir = g_gameRootDir / "mods";
		std::error_code ec;
		if (!fs::exists(modsDir, ec) || !fs::is_directory(modsDir, ec)) {
			_MESSAGE("Mods directory not found: %s", modsDir.string().c_str());
			return;
		}

		std::vector<ModAssetCandidate> candidates;
		CollectModAssetCandidates(modsDir, true, "", candidates);

		std::vector<std::pair<std::string, fs::path>> firstLevelModDirs;
		for (const auto& entry : fs::directory_iterator(modsDir, fs::directory_options::skip_permission_denied, ec)) {
			if (ec) {
				_MESSAGE("Failed to iterate mods root: %s", modsDir.string().c_str());
				break;
			}

			std::error_code dirEc;
			if (!entry.is_directory(dirEc)) {
				continue;
			}

			const auto folderName = entry.path().filename().string();
			firstLevelModDirs.emplace_back(ToLowerAscii(folderName), entry.path());
		}

		std::sort(firstLevelModDirs.begin(), firstLevelModDirs.end(),
			[](const auto& lhs, const auto& rhs) {
				if (lhs.first != rhs.first) {
					return lhs.first < rhs.first;
				}
				return lhs.second.filename().string() < rhs.second.filename().string();
			});

		for (const auto& [folderSortKey, folderPath] : firstLevelModDirs) {
			CollectModAssetCandidates(folderPath, false, folderSortKey, candidates);
		}

		std::sort(candidates.begin(), candidates.end(),
			[](const ModAssetCandidate& lhs, const ModAssetCandidate& rhs) {
				if (lhs.fromModsRoot != rhs.fromModsRoot) {
					return lhs.fromModsRoot && !rhs.fromModsRoot;
				}
				if (!lhs.fromModsRoot && lhs.parentSortKey != rhs.parentSortKey) {
					return lhs.parentSortKey < rhs.parentSortKey;
				}
				if (lhs.fileSortKey != rhs.fileSortKey) {
					return lhs.fileSortKey < rhs.fileSortKey;
				}
				return lhs.filePath.string() < rhs.filePath.string();
			});

		std::size_t conflictCount = 0;
		for (const auto& candidate : candidates) {
			const auto [it, inserted] = g_modAssetOverrides.emplace(candidate.fileHash, candidate.filePath);
			if (!inserted) {
				++conflictCount;
				_MESSAGE("Mod override conflict for 0x%08X: keep=%s, skip=%s",
					candidate.fileHash, it->second.string().c_str(), candidate.filePath.string().c_str());
			}
		}

		_MESSAGE("Mod override index built. candidates=%zu, unique=%zu, conflicts=%zu",
			candidates.size(), g_modAssetOverrides.size(), conflictCount);
	}
}

extern "C" __declspec(dllexport) bool nioh3_plugin_initialize(const Nioh3PluginInitializeParam* param) {
	_MESSAGE("Plugin initialized");
	_MESSAGE("Game version: %s", param->game_version_string);
	_MESSAGE("Plugin dir: %s", param->plugins_dir);
	g_gameRootDir = param->game_root_dir;
	g_pluginsDir = param->plugins_dir;
	BuildModAssetOverrideIndex();

	using FnLoadAssetFromFile =
		bool (*)(uintptr_t, uintptr_t, void*, void*, void*, uintptr_t,
			void*, RdbRuntimeEntryDesc*, IAssetStreamReader*,
			uintptr_t, uintptr_t, void*, void*, bool);

	static auto LoadAssetFromFile = (FnLoadAssetFromFile)HookUtils::ScanIDAPattern(
		"E8 ? ? ? ? 48 8B 8F ? ? ? ? 48 8B 53", 0, 1, 5);
	if (!LoadAssetFromFile) {
		_MESSAGE("Failed to resolve LoadAssetFromFile");
		return false;
	}
	_MESSAGE("LoadAssetFromFile: %p", LoadAssetFromFile);

	using FnGetAssetHandlerFromType =
		IBaseGameAssetHandler * (*)(void*, uint32_t);
	static auto GetAssetHandlerFromType =
		(FnGetAssetHandlerFromType)HookUtils::ReadOffsetData(
			HookUtils::LookupFunctionPattern(
				LoadAssetFromFile, "E8 ? ? ? ? 48 8B 4D ? 48 8B F8 4C 8B C6",
				0x1000),
			1, 5);

	if (!GetAssetHandlerFromType) {
		_MESSAGE("Failed to resolve GetAssetHandlerFromType");
		return false;
	}

	_MESSAGE("GetAssetHandlerFromType: %p", GetAssetHandlerFromType);


	HookLambda(
		LoadAssetFromFile,
		[](uintptr_t a_param1, uintptr_t a_param2, void* a_assetManager,
			void* a_mountCallerCtx, void* a_mountCallbackCtx, uintptr_t a_param6,
			void* a_targetAsset, RdbRuntimeEntryDesc* a_rdbEntryDesc,
			IAssetStreamReader* a_assetStreamReader, uintptr_t a_payloadDataOffset,
			uintptr_t a_payloadSize, void* a_decompressor, void* a_handlerUserCtx,
			bool a_requireDecompress) -> bool {

				do {
					if (!a_assetManager || !a_targetAsset || !a_rdbEntryDesc) {
						break;
					}

					auto fileId = a_rdbEntryDesc->fileKtid;
					if (fileId == 0xFFFFFFFF) {
						break;
					}

					auto typeId = a_rdbEntryDesc->typeInfoKtid;
					// auto *assetHandler = GetAssetHandlerFromType(a_assetManager, typeId);
					// std::string typeName = assetHandler ? assetHandler->GetTypeName() : "Unknown";
					// _MESSAGE("Load asset with hash: 0x%08X | Type: %s (0x%08X)", fileId, typeName.c_str(), typeId);
					// _MESSAGE("fileId: %08X", fileId);
					// _MESSAGE("typeId: %08X", typeId);
					// _MESSAGE("rdbEntryDesc: %p", a_rdbEntryDesc);
					// _MESSAGE("magic: %s", a_rdbEntryDesc->GetMagic().c_str());
					// _MESSAGE("version: %s", a_rdbEntryDesc->GetVersion().c_str());
					// _MESSAGE("sizeInContainer: %08X", a_rdbEntryDesc->sizeInContainer);
					// _MESSAGE("compressedSize: %08X", a_rdbEntryDesc->compressedSize);
					// _MESSAGE("uncompressedSize: %08X", a_rdbEntryDesc->fileSize);
					// _MESSAGE("fileKtid: %08X", a_rdbEntryDesc->fileKtid);
					// _MESSAGE("typeInfoKtid: %08X", a_rdbEntryDesc->typeInfoKtid);
					// _MESSAGE("flags: %d", a_rdbEntryDesc->flags >> 20 & 0x3F);
					// _MESSAGE("f2C: %08X", a_rdbEntryDesc->f2C);
					// _MESSAGE("paramCount: %08X", a_rdbEntryDesc->paramCount);
					// _MESSAGE("f34: %08X", a_rdbEntryDesc->f34);
					// _MESSAGE("paramDataBlock: %p", a_rdbEntryDesc->paramDataBlock);

					auto it = g_modAssetOverrides.find(fileId);
					if (it == g_modAssetOverrides.end()) {
						break;
					}

					const auto& filePath = it->second;
					std::error_code fsEc;
					if (!fs::exists(filePath, fsEc) || !fs::is_regular_file(filePath, fsEc)) {
						break;
					}

					auto reader = std::make_unique<LooseModFileReader>(filePath);
					if (reader->IsOpen()) {
						const auto oldFlags = a_rdbEntryDesc->flags;
						a_rdbEntryDesc->flags = oldFlags | 0x100000;
						auto *assetHandler = GetAssetHandlerFromType(a_assetManager, typeId);
						std::string typeName = assetHandler ? assetHandler->GetTypeName() : "Unknown";
						_MESSAGE("Load mod asset: 0x%08X | Type: %s (0x%08X) | %s",
							fileId, typeName.c_str(), typeId, filePath.string().c_str());
						const auto ret =
							original(a_param1, a_param2, a_assetManager, a_mountCallerCtx, a_mountCallbackCtx,
								a_param6, a_targetAsset, a_rdbEntryDesc,
								reader.get(), a_payloadDataOffset, a_payloadSize,
								nullptr, a_handlerUserCtx, false);
						a_rdbEntryDesc->flags = oldFlags;
						return ret;
					}
					_MESSAGE("Failed to open mod asset file: %s", filePath.string().c_str());
					
				} while (false);

				return original(a_param1, a_param2, a_assetManager, a_mountCallerCtx, a_mountCallbackCtx,
					a_param6, a_targetAsset, a_rdbEntryDesc, a_assetStreamReader,
					a_payloadDataOffset, a_payloadSize, a_decompressor, a_handlerUserCtx,
					a_requireDecompress);
		});

#ifdef _DEBUG
	using FnRegisterAssetHandler = bool (*)(void*, uint32_t, IBaseGameAssetHandler*);
	auto RegisterAssetHandler = (FnRegisterAssetHandler)HookUtils::ScanIDAPattern(
		"E8 ? ? ? ? 84 C0 0F 84 ? ? ? ? 83 65 ? 00 48 8D 15", 0, 1, 5);
	_MESSAGE("RegisterAssetHandler: %p", RegisterAssetHandler);

	HookLambda(RegisterAssetHandler,
		[](void* a_assetManager, uint32_t a_assetHash,
			IBaseGameAssetHandler* a_assetHandler) -> bool {
				char buf[256] = { 0 };
				if (a_assetHandler) {
					do {
						__try {
							const char* typeName = nullptr;
							a_assetHandler->GetTypeName(typeName);
							if (typeName) {
								snprintf(buf, sizeof(buf), "%s", typeName);
							}
							else {
								snprintf(buf, sizeof(buf), "Unknown");
							}
						}
						__except (EXCEPTION_EXECUTE_HANDLER) {
							break;
						}
						_MESSAGE("Asset ID: 0x%08X | %p | %s ", a_assetHash,
							a_assetHandler->GetVtableAddr(), buf);
					} while (0);
				}

				return original(a_assetManager, a_assetHash, a_assetHandler);
	});
#endif
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

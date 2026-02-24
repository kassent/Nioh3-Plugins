#include "RdbTool.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

[[nodiscard]] int GetPid() {
#ifdef _WIN32
    return _getpid();
#else
    return getpid();
#endif
}

[[nodiscard]] std::vector<std::byte> StringToBytes(const std::string& text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (char ch : text) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return out;
}

bool ReadFileBytes(const fs::path& path, std::vector<std::byte>* outBytes) {
    outBytes->clear();
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        return false;
    }
    in.seekg(0, std::ios::beg);
    outBytes->resize(static_cast<std::size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(outBytes->data()), size);
    }
    return static_cast<bool>(in);
}

[[nodiscard]] std::optional<fs::path> FindRepoRoot() {
    fs::path cur = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        if (fs::exists(cur / "plugins/LooseFileLoader/package/root.rdb") &&
            fs::exists(cur / "plugins/LooseFileLoader/package/root.rdx")) {
            return cur;
        }
        if (!cur.has_parent_path()) {
            break;
        }
        cur = cur.parent_path();
    }
    return std::nullopt;
}

bool CopyPackageForTest(const fs::path& srcPackageDir, const fs::path& dstPackageDir) {
    std::error_code ec;
    fs::create_directories(dstPackageDir, ec);
    if (ec) {
        return false;
    }

    const fs::path srcRdb = srcPackageDir / "root.rdb";
    const fs::path srcRdx = srcPackageDir / "root.rdx";
    if (!fs::exists(srcRdb) || !fs::exists(srcRdx)) {
        return false;
    }

    fs::copy_file(srcRdb, dstPackageDir / "root.rdb", fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return false;
    }
    fs::copy_file(srcRdx, dstPackageDir / "root.rdx", fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return false;
    }

    for (const auto& it : fs::directory_iterator(srcPackageDir)) {
        if (!it.is_regular_file()) {
            continue;
        }
        const fs::path p = it.path();
        if (p.extension() == ".fdata") {
            fs::copy_file(p, dstPackageDir / p.filename(), fs::copy_options::overwrite_existing, ec);
            if (ec) {
                return false;
            }
        }
    }

    return true;
}

[[nodiscard]] std::optional<std::uint32_t> PickExtractableEntry(const LooseFileLoader::RdbTool& tool,
                                                                const fs::path& packageDir,
                                                                const fs::path& tempDir) {
    for (const auto& entry : tool.Entries()) {
        if (!entry.hasLocation || entry.fileSize == 0 || entry.location.newFlags != 0x401) {
            continue;
        }
        const fs::path containerPath = packageDir / entry.location.containerPath;
        if (!fs::exists(containerPath)) {
            continue;
        }

        const fs::path probeOut = tempDir / "probe_extract.bin";
        std::string error;
        if (tool.Extract(entry.fileKtid, probeOut, &error)) {
            std::error_code ec;
            const std::uint64_t size = fs::file_size(probeOut, ec);
            fs::remove(probeOut, ec);
            if (!ec && size > 0) {
                return entry.fileKtid;
            }
        }
    }
    return std::nullopt;
}

bool BytesEqual(const std::vector<std::byte>& lhs, const std::vector<std::byte>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    return std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

}  // namespace

#ifdef LOOSEFILELOADER_RDB_TOOL_TEST_MAIN
int main() {
    const auto repoRoot = FindRepoRoot();
    if (!repoRoot.has_value()) {
        std::cerr << "[FAIL] Could not locate repository root.\n";
        return 1;
    }

    const fs::path srcPackageDir = *repoRoot / "plugins/LooseFileLoader/package";
    const fs::path testRoot = fs::temp_directory_path() / ("LooseFileLoader_RdbToolTest_" + std::to_string(GetPid()));
    const fs::path dstPackageDir = testRoot / "package";

    std::error_code ec;
    fs::remove_all(testRoot, ec);
    ec.clear();

    if (!CopyPackageForTest(srcPackageDir, dstPackageDir)) {
        std::cerr << "[FAIL] Failed to prepare test package directory.\n";
        return 1;
    }

    const fs::path rootRdb = dstPackageDir / "root.rdb";
    const fs::path rootRdx = dstPackageDir / "root.rdx";

    std::string error;
    auto toolOpt = LooseFileLoader::RdbTool::Open(rootRdb, rootRdx, &error);
    if (!toolOpt.has_value()) {
        std::cerr << "[FAIL] Open failed: " << error << "\n";
        return 1;
    }
    LooseFileLoader::RdbTool tool = std::move(*toolOpt);

    if (tool.Entries().empty()) {
        std::cerr << "[FAIL] Parsed entry list is empty.\n";
        return 1;
    }

    const fs::path dumpPath = testRoot / "dump_before.txt";
    if (!tool.Dump(dumpPath, &error) || !fs::exists(dumpPath)) {
        std::cerr << "[FAIL] Dump failed: " << error << "\n";
        return 1;
    }

    const auto templateKtid = PickExtractableEntry(tool, dstPackageDir, testRoot);
    if (!templateKtid.has_value()) {
        std::cerr << "[FAIL] Could not find an extractable internal entry.\n";
        return 1;
    }

    const fs::path extractPath = testRoot / "extract_original.bin";
    if (!tool.Extract(*templateKtid, extractPath, &error)) {
        std::cerr << "[FAIL] Extract original failed: " << error << "\n";
        return 1;
    }

    std::vector<std::byte> replacementData = StringToBytes("RDB_TOOL_REPLACE_PAYLOAD_TEST_0123456789");
    if (!tool.Replace(*templateKtid, replacementData, &error)) {
        std::cerr << "[FAIL] Replace failed: " << error << "\n";
        return 1;
    }

    toolOpt = LooseFileLoader::RdbTool::Open(rootRdb, rootRdx, &error);
    if (!toolOpt.has_value()) {
        std::cerr << "[FAIL] Re-open after replace failed: " << error << "\n";
        return 1;
    }
    tool = std::move(*toolOpt);

    const fs::path replacedExtractPath = testRoot / "extract_replaced.bin";
    if (!tool.Extract(*templateKtid, replacedExtractPath, &error)) {
        std::cerr << "[FAIL] Extract replaced failed: " << error << "\n";
        return 1;
    }

    std::vector<std::byte> replacedBytes;
    if (!ReadFileBytes(replacedExtractPath, &replacedBytes)) {
        std::cerr << "[FAIL] Unable to read replaced extract file.\n";
        return 1;
    }
    if (!BytesEqual(replacedBytes, replacementData)) {
        std::cerr << "[FAIL] Replaced payload bytes mismatch.\n";
        return 1;
    }

    std::uint32_t reuseFileKtid = 0xF0ABB000u;
    while (tool.FindEntryByFileKtid(reuseFileKtid) != nullptr) {
        ++reuseFileKtid;
    }

    if (!tool.Insert(reuseFileKtid, *templateKtid, true, 0, &error)) {
        std::cerr << "[FAIL] Reuse insert failed: " << error << "\n";
        return 1;
    }

    toolOpt = LooseFileLoader::RdbTool::Open(rootRdb, rootRdx, &error);
    if (!toolOpt.has_value()) {
        std::cerr << "[FAIL] Re-open after reuse insert failed: " << error << "\n";
        return 1;
    }
    tool = std::move(*toolOpt);

    const fs::path reuseExtractPath = testRoot / "extract_reuse.bin";
    if (!tool.Extract(reuseFileKtid, reuseExtractPath, &error)) {
        std::cerr << "[FAIL] Extract reuse entry failed: " << error << "\n";
        return 1;
    }

    std::vector<std::byte> reuseBytes;
    if (!ReadFileBytes(reuseExtractPath, &reuseBytes)) {
        std::cerr << "[FAIL] Unable to read reuse extract file.\n";
        return 1;
    }
    if (!BytesEqual(reuseBytes, replacementData)) {
        std::cerr << "[FAIL] Reuse payload bytes mismatch.\n";
        return 1;
    }

    std::uint32_t newFileKtid = 0xF0ABC000u;
    while (tool.FindEntryByFileKtid(newFileKtid) != nullptr) {
        ++newFileKtid;
    }

    std::vector<std::byte> insertData = StringToBytes("RDB_TOOL_INSERT_PAYLOAD_TEST_ABCDEFGHIJ");
    constexpr std::uint32_t customTypeInfo = 0xBBD39F2D;
    if (!tool.Insert(newFileKtid, *templateKtid, insertData, customTypeInfo, false, &error)) {
        std::cerr << "[FAIL] Insert failed: " << error << "\n";
        return 1;
    }

    toolOpt = LooseFileLoader::RdbTool::Open(rootRdb, rootRdx, &error);
    if (!toolOpt.has_value()) {
        std::cerr << "[FAIL] Re-open after insert failed: " << error << "\n";
        return 1;
    }
    tool = std::move(*toolOpt);

    const auto* insertedEntry = tool.FindEntryByFileKtid(newFileKtid);
    if (insertedEntry == nullptr) {
        std::cerr << "[FAIL] Inserted entry not found after reopen.\n";
        return 1;
    }
    if (insertedEntry->typeInfoKtid != customTypeInfo) {
        std::cerr << "[FAIL] Inserted entry typeInfoKtid mismatch.\n";
        return 1;
    }

    const fs::path insertedExtractPath = testRoot / "extract_inserted.bin";
    if (!tool.Extract(newFileKtid, insertedExtractPath, &error)) {
        std::cerr << "[FAIL] Extract inserted failed: " << error << "\n";
        return 1;
    }

    std::vector<std::byte> insertedBytes;
    if (!ReadFileBytes(insertedExtractPath, &insertedBytes)) {
        std::cerr << "[FAIL] Unable to read inserted extract file.\n";
        return 1;
    }
    if (!BytesEqual(insertedBytes, insertData)) {
        std::cerr << "[FAIL] Inserted payload bytes mismatch.\n";
        return 1;
    }

    const fs::path dumpAfter = testRoot / "dump_after.txt";
    if (!tool.Dump(dumpAfter, &error)) {
        std::cerr << "[FAIL] Dump after modifications failed: " << error << "\n";
        return 1;
    }

    std::cout << "[PASS] RdbTool tests passed.\n";
    std::cout << "Test workspace: " << testRoot.string() << "\n";
    return 0;
}
#endif

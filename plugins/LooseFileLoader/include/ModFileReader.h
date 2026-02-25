#pragma once

#include "Common.h"

#include <filesystem>

#include "binary_io/binary_io.hpp"

namespace LooseFileLoader {

class ModFileReader final : public IAssetStreamReader {
public:
    ModFileReader() = delete;
    explicit ModFileReader(std::filesystem::path filePath);
    ~ModFileReader() override;

    bool Open(std::filesystem::path filePath);
    void Close() override;
    std::int64_t Skip(std::int64_t deltaBytes) override;
    std::uint64_t ReadByte(std::uint8_t* outByte) override;
    std::uint64_t Read(void* dst, std::uint64_t dstOffset, std::uint64_t size) override;
    std::uint64_t QueryCapability() override;

    [[nodiscard]] bool IsOpen() const;

private:
    std::filesystem::path filePath_{};
    binary_io::file_istream stream_{};
    std::uint64_t fileSize_ = 0;
};

}  // namespace LooseFileLoader


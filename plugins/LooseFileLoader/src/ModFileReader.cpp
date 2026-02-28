#include "ModFileReader.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>

namespace fs = std::filesystem;

namespace LooseFileLoader {

ModFileReader::ModFileReader(fs::path filePath) {
    Open(std::move(filePath));
}

ModFileReader::~ModFileReader() {
    Close();
}

bool ModFileReader::Open(fs::path filePath) {
    if (stream_.is_open()) {
        Close();
    }

    filePath_ = std::move(filePath);
    if (filePath_.empty() || !fs::exists(filePath_) || !fs::is_regular_file(filePath_)) {
        return false;
    }

    try {
        fileSize_ = fs::file_size(filePath_);
        stream_.open(filePath_);
    } catch (...) {
        Close();
        return false;
    }
    return stream_.is_open();
}

void ModFileReader::Close() {
    if (stream_.is_open()) {
        stream_.close();
    }
}

std::int64_t ModFileReader::Skip(std::int64_t deltaBytes) {
    if (!stream_.is_open()) {
        return 0;
    }

    const std::int64_t current = static_cast<std::int64_t>(stream_.tell());
    const std::int64_t end = static_cast<std::int64_t>(fileSize_);
    const std::int64_t target = std::clamp(current + deltaBytes, std::int64_t{0}, end);
    stream_.seek_absolute(static_cast<binary_io::streamoff>(target));
    return target - current;
}

std::uint64_t ModFileReader::ReadByte(std::uint8_t* outByte) {
    if (outByte == nullptr) {
        return 0;
    }
    return Read(outByte, 0, 1);
}

std::uint64_t ModFileReader::Read(void* dst, std::uint64_t dstOffset, std::uint64_t size) {
    if (!stream_.is_open() || dst == nullptr || size == 0) {
        return 0;
    }

    const std::uint64_t current = static_cast<std::uint64_t>(stream_.tell());
    const std::uint64_t remain = (current < fileSize_) ? (fileSize_ - current) : 0;
    const std::uint64_t toRead = std::min(size, remain);
    if (toRead == 0) {
        return 0;
    }

    auto* out = static_cast<std::byte*>(dst) + dstOffset;
    const auto before = stream_.tell();
    try {
        stream_.read_bytes(std::span<std::byte>(out, static_cast<std::size_t>(toRead)));
    } catch (const binary_io::buffer_exhausted&) {
    }
    const auto after = stream_.tell();
    return (after > before) ? static_cast<std::uint64_t>(after - before) : 0;
}

std::uint64_t ModFileReader::GetID() const {
    return kModFileReaderId;
}

bool ModFileReader::IsOpen() const {
    return stream_.is_open();
}

std::uint64_t ModFileReader::GetFileSize() const {
    return fileSize_;
}

std::string ModFileReader::GetFilePath() const {
    return filePath_.generic_string();
}

}  // namespace LooseFileLoader


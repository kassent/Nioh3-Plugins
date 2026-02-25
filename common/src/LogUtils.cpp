#include "LogUtils.h"
#include "FileUtils.h"
#include <cstdarg>
#include <memory>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>

#pragma warning(disable: 4073)	// yes this is intentional
#pragma init_seg(lib)

std::shared_ptr<spdlog::logger> globalLogger = []()->auto {
    auto exeDir = FileUtils::GetExecutableDirectory();
    auto logFilePath = exeDir / "logs" / (FileUtils::GetCurrentModuleName() + ".log");
    // Create a file rotating logger with 5 MB size max and 3 rotated files
    auto max_size = 1048576 * 5;
    auto max_files = 1;
    auto logger = spdlog::rotating_logger_mt("logger", logFilePath.string(), max_size, max_files);
    logger->flush_on(spdlog::level::debug);
    logger->set_level(spdlog::level::info);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%l][%t] %v");
    logger->info("===============================================================");
    return logger;
}();


void _MESSAGE(const char* fmt, ...) {
    if (fmt == nullptr) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    auto len = vsnprintf(nullptr, 0, fmt, args);
    if (len <= 0) {
        return;
    }
    std::string buffer;
    buffer.resize(len + 1, 0);
    vsnprintf(buffer.data(), buffer.size(), fmt, args);
    va_end(args);

    if (globalLogger) {
        globalLogger->info(buffer);
    }
}

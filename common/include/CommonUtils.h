#pragma once

#include <string>
#include <cstdint>
#include <string_view>


namespace CommonUtils {
    std::string ConvertWStringToCString(std::wstring_view wstr);
    void DumpClass(void * theClassPtr, uint64_t nIntsToDump);
    std::string ToLowerAscii(std::string_view s);
    std::wstring ToLowerAscii(std::wstring_view s);
}
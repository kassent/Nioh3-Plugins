#pragma once
#include <string_view>
// Logging related
void initLogger(const char* pluginName);
void _MESSAGE(const char* fmt, ...);

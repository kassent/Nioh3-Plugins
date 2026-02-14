# 收集common库的源文件
file(GLOB_RECURSE COMMON_SOURCES 
    "src/*.cpp"
    "src/*.c"
    "src/LightningScanner/*.cpp"
    "include/*.h"
    "include/*.hpp"
    "include/reframework/*.h"
    "include/reframework/*.hpp"

)


# 创建静态库
add_library(common_lib STATIC ${COMMON_SOURCES})

# 设置包含目录
target_include_directories(common_lib
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/lua
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 链接公共依赖
target_link_libraries(common_lib
    PUBLIC
        spdlog::spdlog_header_only
        fmt::fmt
        nlohmann_json::nlohmann_json
)

if(MSVC)
    # MSVC特定编译选项
    target_compile_options(common_lib PRIVATE 
        /wd4244 /wd4819 /wd4068 /utf-8
        $<$<CONFIG:Release>:/MT> 
        $<$<CONFIG:Debug>:/MTd>
    )
endif()

# 导出所有符号
set_target_properties(common_lib PROPERTIES 
    WINDOWS_EXPORT_ALL_SYMBOLS ON
) 
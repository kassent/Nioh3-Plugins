cmake_minimum_required(VERSION 3.21)

# 设置主项目名称
project(Nioh3Plugins)

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static")
    set(VCPKG_LIBRARY_LINKAGE "static")
    set(CMAKE_TOOLCHAIN_FILE "D:/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake" 
        CACHE STRING "Vcpkg toolchain file")
    include(D:/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake)
endif()

# 设置C++标准
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(ZYDIS_STATIC_BUILD ON)
# 设置所有子项目的输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

if ("${CMAKE_BUILD_TYPE}" MATCHES "Release")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MT")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MT") 

    # Statically compile runtime
    string(REGEX REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "/MD" "/MT" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    string(REGEX REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    string(REGEX REPLACE "/MD" "/MT" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
    
    message(NOTICE "Building in Release mode")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# 查找公共依赖包
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

# 定义一个函数用于设置子项目的通用属性
function(configure_plugin_project TARGET_NAME)
    # 设置为动态库
    add_library(${TARGET_NAME} SHARED)

    # 设置包含目录
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/common/include
        ${CMAKE_SOURCE_DIR}/common/include/lua
        ${CMAKE_SOURCE_DIR}/common/include/sol2/single/include
    )

    # 链接公共依赖库
    target_link_libraries(${TARGET_NAME} PRIVATE
        common_lib # 链接common静态库
        spdlog::spdlog_header_only
        fmt::fmt
        nlohmann_json::nlohmann_json
        $<$<BOOL:${WIN32}>:iphlpapi>
        ZLIB::ZLIB
    )

    target_compile_features(${TARGET_NAME} PUBLIC
		cxx_std_23
	)

    if(MSVC)
        # MSVC特定编译选项
        target_compile_options(${TARGET_NAME} PRIVATE 
            /wd4244 /wd4819 /wd4068 /utf-8
            $<$<CONFIG:Release>:/MT> 
            $<$<CONFIG:Debug>:/MTd>
        )
        
        # 设置SUBSYSTEM为WINDOWS
        set_target_properties(${TARGET_NAME} PROPERTIES 
            LINK_FLAGS "/SUBSYSTEM:WINDOWS"
        )
    endif()

    # 设置调试器命令
    set_target_properties(${TARGET_NAME} PROPERTIES
        VS_DEBUGGER_COMMAND "H:/SteamLibrary/steamapps/common/Nioh3/Nioh3.exe"
    )

    # 添加后期构建命令，复制DLL到Steam目录
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:${TARGET_NAME}>
            "E:/SteamLibrary/steamapps/common/Nioh3/plugins/$<TARGET_FILE_NAME:${TARGET_NAME}>"
        COMMENT "Copying ${TARGET_NAME} to E:/SteamLibrary/steamapps/common/Nioh3/plugins"
    )
endfunction()

# 首先添加common静态库
add_subdirectory(common)

# 自动查找并添加所有插件子项目
file(GLOB PLUGIN_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "plugins/*")
foreach(PLUGIN_DIR ${PLUGIN_DIRS})
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN_DIR})
        add_subdirectory(${PLUGIN_DIR})
    endif()
endforeach()

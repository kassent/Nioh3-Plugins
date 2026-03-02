# 获取当前文件夹名称作为项目名称
get_filename_component(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${PROJECT_NAME})

# 收集源文件
file(GLOB_RECURSE SOURCES 
    "*.cpp"
    "*.h"
)

# 配置插件项目
configure_plugin_project(${PROJECT_NAME})

# 添加源文件
target_sources(${PROJECT_NAME} PRIVATE ${SOURCES})

# 添加common库的头文件路径
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/common/include
    ${CMAKE_SOURCE_DIR}/common/include/common
) 

target_compile_features(${PROJECT_NAME} PUBLIC
    cxx_std_23
)
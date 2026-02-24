get_filename_component(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${PROJECT_NAME})

set(SOURCES
    main.cpp
)

file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "include/*.h"
)
list(FILTER MODULE_SOURCES EXCLUDE REGEX ".*RdbToolTests\\.cpp$")
list(APPEND SOURCES ${MODULE_SOURCES})

configure_plugin_project(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/common/include
    ${CMAKE_SOURCE_DIR}/common/include/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(${PROJECT_NAME} PUBLIC
    cxx_std_23
)

add_executable(${PROJECT_NAME}RdbToolTests
    src/RdbTool.cpp
    src/RdbToolTests.cpp
    include/RdbTool.h
)
target_compile_definitions(${PROJECT_NAME}RdbToolTests PRIVATE
    LOOSEFILELOADER_RDB_TOOL_TEST_MAIN=1
)
target_include_directories(${PROJECT_NAME}RdbToolTests PRIVATE
    ${CMAKE_SOURCE_DIR}/common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(${PROJECT_NAME}RdbToolTests PRIVATE
    common_lib
    ZLIB::ZLIB
)
target_compile_features(${PROJECT_NAME}RdbToolTests PUBLIC
    cxx_std_23
)

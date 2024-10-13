cmake_minimum_required(VERSION 3.20)
project(TinyWebServer)

set(CMAKE_CXX_STANDARD 20)

# Release        - 添加 -O3 -DNDEBUG 标志
# Debug          - 添加 -g 标志
# MinSizeRel     - 添加 -Os -DNDEBUG
# RelWithDebInfo - 添加 -O2 -g -DNDEBUG 标志

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
elseif (CMAKE_BUILD_TYPE MATCHES "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -O0 -rdynamic -Wall")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -rdynamic -Wall")
else ()
    set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -Wall")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
endif ()

file(GLOB APP_BUFFER
        "app/buffer/*.cpp"
        "app/buffer/*.hpp"
)

file(GLOB APP_LOGGER
        "app/logger/*.cpp"
        "app/logger/*.hpp"
)

file(GLOB APP_CONFIG
        "app/config/*.cpp"
        "app/config/*.hpp"
)

file(GLOB APP_HTTP
        "app/http/*.cpp"
        "app/http/*.hpp"
)

file(GLOB APP_POOL
        "app/pool/*.cpp"
        "app/pool/*.hpp"
)

file(GLOB APP_SERVER
        "app/server/*.cpp"
        "app/server/*.hpp"
)

file(GLOB APP_TIMER
        "app/timer/*.cpp"
        "app/timer/*.hpp"
)

# 使用 pkg-config
find_package(PkgConfig REQUIRED)
# 查找 MariaDB 库
pkg_check_modules(MARIADB REQUIRED mariadb)
# Google Test
#find_package(GTest REQUIRED)
#include_directories(${GTEST_INCLUDE_DIRS})

add_executable(${PROJECT_NAME} main.cpp
        ${APP_BUFFER}
        ${APP_LOGGER}
        ${APP_CONFIG}
        ${APP_HTTP}
        ${APP_POOL}
        ${APP_SERVER}
        ${APP_TIMER}
)

if (CMAKE_BUILD_TYPE MATCHES "Debug")
    message("HANDLE_BACKTRACE DEFINE")
    target_compile_definitions(${PROJECT_NAME} PRIVATE HANDLE_BACKTRACE)
endif ()

include(FetchContent)

FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/Liccsu/yaml-cpp.git
        # version ^= 0.8.0
        GIT_TAG master # Can be a tag (yaml-cpp-x.x.x), a commit hash, or a branch name (master)
)
FetchContent_GetProperties(yaml-cpp)

if (NOT yaml-cpp_POPULATED)
    message(STATUS "Fetching yaml-cpp...")
    FetchContent_Populate(yaml-cpp)
    add_subdirectory(${yaml-cpp_SOURCE_DIR} ${yaml-cpp_BINARY_DIR})
endif ()

# 链接库
target_include_directories(${PROJECT_NAME} PRIVATE ${MARIADB_INCLUDE_DIRS})
# The library or executable that require yaml-cpp library
target_link_libraries(${PROJECT_NAME} PRIVATE ${MARIADB_LIBRARIES} PUBLIC yaml-cpp::yaml-cpp)

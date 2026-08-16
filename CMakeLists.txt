cmake_minimum_required(VERSION 3.20)
project(TinyWebServer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# 选项
# ---------------------------------------------------------------------------
option(BUILD_TESTING "Build unit/integration tests (GoogleTest)" ON)
option(ENABLE_SANITIZER "Build with AddressSanitizer + UBSan" OFF)
option(ENABLE_TSAN "Build with ThreadSanitizer (bare-metal Linux only; WSL2/容器不可靠)" OFF)
option(ENABLE_WERROR "Treat compiler warnings as errors (CI use)" OFF)

# ---------------------------------------------------------------------------
# 依赖
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(MARIADB REQUIRED IMPORTED_TARGET mariadb)

# yaml-cpp：优先系统包；缺失时 FetchContent 从维护者 fork 获取
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    include(FetchContent)
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
            yaml-cpp
            GIT_REPOSITORY https://github.com/Liccsu/yaml-cpp.git
            GIT_TAG master
            GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(yaml-cpp)
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# 编译告警
# ---------------------------------------------------------------------------
set(WARNING_FLAGS -Wall -Wextra -Wpedantic -Wshadow -Wformat=2)
if(ENABLE_WERROR)
    list(APPEND WARNING_FLAGS -Werror)
endif()
add_compile_options(${WARNING_FLAGS})

if(ENABLE_SANITIZER)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# TSan 与 ASan/UBSan 互斥（GCC 链接冲突）
if(ENABLE_TSAN AND NOT ENABLE_SANITIZER)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

# ---------------------------------------------------------------------------
# 核心库（与平台无关的服务器逻辑；仅 Linux）
# ---------------------------------------------------------------------------
add_library(webserver_core STATIC
        src/net/Buffer.cpp
        src/net/Epoller.cpp
        src/net/Acceptor.cpp
        src/http/HttpRequest.cpp
        src/http/HttpResponse.cpp
        src/http/HttpConnection.cpp
        src/timer/TimerHeap.cpp
        src/log/Logger.cpp
        src/log/AsyncLogging.cpp
        src/log/LogFile.cpp
        src/config/Config.cpp
        src/storage/SqlConnPool.cpp
        src/app/ConnectionManager.cpp
        src/app/WebServer.cpp
)
target_include_directories(webserver_core PUBLIC src)
# yaml-cpp 与 MariaDB 头文件被公开头引用，需 PUBLIC 传播
target_link_libraries(webserver_core
        PUBLIC yaml-cpp::yaml-cpp PkgConfig::MARIADB Threads::Threads
)

# ---------------------------------------------------------------------------
# 可执行文件
# ---------------------------------------------------------------------------
add_executable(webserver src/app/main.cpp)
target_link_libraries(webserver PRIVATE webserver_core)

# ---------------------------------------------------------------------------
# 测试
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
    enable_testing()

    # GTest 优先使用系统包；缺失时回退 FetchContent（GitHub 网络受限环境可用 -DBUILD_TESTING=OFF）
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
                googletest
                GIT_REPOSITORY https://github.com/google/googletest.git
                GIT_TAG v1.14.0
                GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(unit_tests
            tests/unit/test_main.cpp
            tests/unit/test_buffer.cpp
            tests/unit/test_http_request.cpp
            tests/unit/test_http_response.cpp
            tests/unit/test_url_decode.cpp
            tests/unit/test_timer.cpp
            tests/unit/test_thread_pool.cpp
            tests/unit/test_log_stream.cpp
            tests/unit/test_config.cpp
    )
    target_link_libraries(unit_tests PRIVATE webserver_core GTest::gtest GTest::gtest_main)
    add_test(NAME unit_tests COMMAND unit_tests)

    # 集成测试：真实进程 + raw socket 客户端
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME integration_tests
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/run_integration_tests.py
            $<TARGET_FILE:webserver> ${CMAKE_SOURCE_DIR}
    )
    set_tests_properties(integration_tests PROPERTIES TIMEOUT 180)
endif()

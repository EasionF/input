cmake_minimum_required(VERSION 3.22)
project(netroom LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC 显式 /std:c++17 + UTF-8 源文件
if(MSVC)
    add_compile_options(/std:c++17 /utf-8 /W4 /permissive- /EHsc)
endif()

add_library(netroom_ipc STATIC
    netroom/ipc/ring_buffer.cpp
    netroom/ipc/ime_channel.cpp
    netroom/ipc/frame_codec.cpp
)
target_include_directories(netroom_ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Threads REQUIRED)
target_link_libraries(netroom_ipc PUBLIC Threads::Threads)

option(NETROOM_BUILD_TESTS "build unit tests" ON)
if(NETROOM_BUILD_TESTS)
    enable_testing()
    add_executable(test_ring tests/test_ring.cpp)
    target_link_libraries(test_ring PRIVATE netroom_ipc)
    add_test(NAME ring COMMAND test_ring)
    add_executable(test_channel tests/test_channel.cpp)
    target_link_libraries(test_channel PRIVATE netroom_ipc)
    add_test(NAME channel COMMAND test_channel)
    add_executable(test_codec tests/test_codec.cpp)
    target_link_libraries(test_codec PRIVATE netroom_ipc)
    add_test(NAME codec COMMAND test_codec)
endif()

# ---- 净室 TSF 前端壳（宿主 DLL，注入目标应用进程） ----
option(NETROOM_BUILD_TFS "" ON)
if(NETROOM_BUILD_TFS)
    add_library(netroom_tsf SHARED
        netroom/tfs_host/tfs_module.cpp
        netroom/tfs_host/tfs_service.cpp
        netroom/tfs_host/netroom_tsf.def
    )
    target_include_directories(netroom_tsf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(netroom_tsf PRIVATE netroom_ipc netroom_host)
    target_compile_definitions(netroom_tsf PRIVATE UNICODE _UNICODE IME_TSF_BUILD)
    set_target_properties(netroom_tsf PROPERTIES
        OUTPUT_NAME "netroom_tsf"
        PREFIX "")
endif()
# ---- 输入引擎：内置拼音（M0 落底实现） ----
add_library(netroom_engine STATIC
    netroom/daemon/engine.h
    netroom/daemon/pinyin_engine.h
    netroom/daemon/pinyin_engine.cpp
)
target_include_directories(netroom_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(netroom_engine PUBLIC netroom_ipc)

add_executable(netroom_daemon netroom/daemon/daemon_main.cpp)
target_link_libraries(netroom_daemon PRIVATE netroom_engine)

# 引擎单元 + 端到端 daemon 进程集成（需构建 netroom_daemon.exe）
option(NETROOM_BUILD_DAEMON_TESTS "" ON)
if(NETROOM_BUILD_DAEMON_TESTS)
    add_executable(test_pinyin_engine tests/test_pinyin_engine.cpp)
    target_link_libraries(test_pinyin_engine PRIVATE netroom_engine)
    add_test(NAME pinyin COMMAND test_pinyin_engine)
    add_executable(test_start_daemon tests/test_start_daemon.cpp)
    target_link_libraries(test_start_daemon PRIVATE netroom_engine)
    add_test(NAME daemon_integration COMMAND test_start_daemon)
    set_tests_properties(daemon_integration PROPERTIES TIMEOUT 60)
endif()

# ---- 宿主组合语义（无 TSF 依赖，可无头单测） ----
add_library(netroom_host STATIC
    netroom/host/composition_kit.h
    netroom/host/composition_kit.cpp
)
target_include_directories(netroom_host PUBLIC )
target_link_libraries(netroom_host PUBLIC netroom_ipc)

add_executable(test_composition_kit tests/test_composition_kit.cpp)
target_link_libraries(test_composition_kit PRIVATE netroom_host)
add_test(NAME composition_kit COMMAND test_composition_kit)

cmake_minimum_required(VERSION 3.22)
project(netroom LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC 显式 /std:c++17 + UTF-8 源文件
if(MSVC)
    add_compile_options(/std:c++17 /utf-8 /W4 /permissive-)
endif()

add_library(netroom_ipc STATIC
    netroom/ipc/ring_buffer.cpp
    netroom/ipc/ime_channel.cpp
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
endif()
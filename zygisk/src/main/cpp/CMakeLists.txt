cmake_minimum_required(VERSION 3.18.1)
project("mirage_forge")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti -fvisibility=hidden -fvisibility-inlines-hidden -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -O3 -ffunction-sections -fdata-sections")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections -Wl,--exclude-libs,ALL -Wl,-s")

add_library(mirage_forge SHARED
    mirage_forge.cpp
)

# 同时包含 cpp 根目录与 include 子目录，无论 zygisk.hpp 放在哪都能找到
target_include_directories(mirage_forge PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# 直接链接 NDK Sysroot 内置的 log 与 dl 库，杜绝 find_library 路径粘连
target_link_libraries(mirage_forge PRIVATE
    log
    dl
)

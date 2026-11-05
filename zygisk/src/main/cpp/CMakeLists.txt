cmake_minimum_required(VERSION 3.18.1)
project("mirage_forge")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti -fvisibility=hidden -fvisibility-inlines-hidden -Wall -Wextra -Wno-unused-parameter -O3 -ffunction-sections -fdata-sections")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections -Wl,--exclude-libs,ALL -Wl,-s")

add_library(mirage_forge SHARED
    mirage_forge.cpp
)

# 显式指定源码根目录与 include 子目录，彻底解决 'zygisk.hpp' file not found
target_include_directories(mirage_forge PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

find_library(log-lib log)
find_library(dl-lib dl)

target_link_libraries(mirage_forge
    ${log-lib}${dl-lib}
)

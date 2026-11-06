cmake_minimum_required(VERSION 3.18.1)
project("mirageforge_cli")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter -O3 -ffunction-sections -fdata-sections -fPIE")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections -Wl,-s -pie -static-libstdc++")

add_executable(mirageforge_cli
    src/main.cpp
)

target_include_directories(mirageforge_cli PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(mirageforge_cli PRIVATE
    log
)

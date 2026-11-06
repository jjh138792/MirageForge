cmake_minimum_required(VERSION 3.18.1)
project("mirageforge_cli")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIE -fno-exceptions -fno-rtti -Wall -Wextra -Wno-unused-parameter -O3 -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pie -static-libstdc++ -Wl,--gc-sections -Wl,-s")

add_executable(mirageforge_cli
    main.cpp
)

target_include_directories(mirageforge_cli PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(mirageforge_cli PRIVATE
    log
)

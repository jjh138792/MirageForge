cmake_minimum_required(VERSION 3.18.1)
project("mirage_forge")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter -Oz")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections -Wl,-s")

add_library(mirage_forge SHARED
    mirage_forge.cpp
)

find_library(log-lib log)
target_link_libraries(mirage_forge ${log-lib})

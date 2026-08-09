cmake_minimum_required(VERSION 3.16)
project(genea_video_streamer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
)
pkg_check_modules(YAML_CPP REQUIRED IMPORTED_TARGET yaml-cpp)

add_executable(streamer
    src/main.cpp
    src/capture/RtspSource.cpp
    src/util/Config.cpp
    src/util/PacketClock.cpp
)

target_include_directories(streamer PRIVATE include)

target_link_libraries(streamer PRIVATE
    PkgConfig::LIBAV
    PkgConfig::YAML_CPP
)

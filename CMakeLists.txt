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
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

add_executable(streamer
    src/main.cpp
    src/capture/RtspSource.cpp
    src/util/Config.cpp
    src/util/PacketClock.cpp
    src/util/ReconnectPolicy.cpp
    src/util/PipelineHealth.cpp
    src/util/StreamWorker.cpp
    src/util/StreamManager.cpp
    src/server/HttpServer.cpp
    src/mux/StreamCopyPlanner.cpp
    src/mux/HlsMuxer.cpp
)

target_include_directories(streamer PRIVATE include)

target_link_libraries(streamer PRIVATE
    PkgConfig::LIBAV
    PkgConfig::YAML_CPP
    PkgConfig::SQLITE3
)

# Unit tests
enable_testing()

# ReconnectPolicy tests
add_executable(test_reconnect_policy
    tests/unit/test_reconnect_policy.cpp
    src/util/ReconnectPolicy.cpp
)
target_include_directories(test_reconnect_policy PRIVATE include)

# PipelineHealth tests
add_executable(test_pipeline_health
    tests/unit/test_pipeline_health.cpp
    src/util/PipelineHealth.cpp
)
target_include_directories(test_pipeline_health PRIVATE include)

add_test(NAME ReconnectPolicyTests COMMAND test_reconnect_policy)
add_test(NAME PipelineHealthTests COMMAND test_pipeline_health)

# Integration tests
add_executable(test_reconnect_scenario
    tests/integration/test_reconnect_scenario.cpp
)
target_include_directories(test_reconnect_scenario PRIVATE include)

add_test(NAME ReconnectScenarioTests COMMAND test_reconnect_scenario)

# HTTP Server integration tests (Phase 6.6)
# Links against the full streaming stack to exercise HttpServer + StreamManager
# without starting RTSP connections (StreamManager constructor creates workers;
# getStream() works on never-started workers — zero RTSP activity in tests).
add_executable(test_http_server
    tests/unit/test_http_server.cpp
    src/capture/RtspSource.cpp
    src/util/Config.cpp
    src/util/PacketClock.cpp
    src/util/ReconnectPolicy.cpp
    src/util/PipelineHealth.cpp
    src/util/StreamWorker.cpp
    src/util/StreamManager.cpp
    src/server/HttpServer.cpp
    src/mux/StreamCopyPlanner.cpp
    src/mux/HlsMuxer.cpp
)
target_include_directories(test_http_server PRIVATE include)
target_link_libraries(test_http_server PRIVATE
    PkgConfig::LIBAV
    PkgConfig::YAML_CPP
    PkgConfig::SQLITE3
)
add_test(NAME HttpServerTests COMMAND test_http_server)


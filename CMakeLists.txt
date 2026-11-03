cmake_minimum_required(VERSION 3.16)
project(network-camera)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# OpenSSL
find_package(OpenSSL REQUIRED)

# cpp-httplib (HTTPS)
set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "" FORCE)
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.14.3
    GIT_SHALLOW TRUE
)

# nlohmann/json
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(cpp_httplib nlohmann_json)

# GStreamer
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-rtsp-server-1.0)

# ePTZ crop library
add_library(eptz_crop STATIC src/pipeline/EptzCrop.cpp)
target_include_directories(eptz_crop PUBLIC src/pipeline)

# Main executable
add_executable(network-camera 
    src/main.cpp 
    src/pipeline/EptzController.cpp
    src/rtsp/RtspServer.cpp
    src/http/HttpServer.cpp
    src/http/AuthManager.cpp
    )
target_include_directories(network-camera PRIVATE src)
target_link_libraries(network-camera PRIVATE
    eptz_crop
    PkgConfig::GST
    httplib::httplib
    nlohmann_json::nlohmann_json
    OpenSSL::Crypto
    )

# Tests
enable_testing()

add_executable(test_eptz_crop src/tests/test_eptz_crop.cpp)
target_link_libraries(test_eptz_crop PRIVATE eptz_crop)
add_test(NAME eptz_crop COMMAND test_eptz_crop)
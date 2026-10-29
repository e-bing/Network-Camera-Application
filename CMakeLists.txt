cmake_minimum_required(VERSION 3.16)
project(network-camera)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-rtsp-server-1.0)

add_executable(network-camera 
    src/main.cpp 
    src/pipeline/EptzController.cpp
    src/rtsp/RtspServer.cpp)
target_include_directories(network-camera PRIVATE src)
target_link_libraries(network-camera PRIVATE eptz_crop PkgConfig::GST)

add_library(eptz_crop STATIC src/pipeline/EptzCrop.cpp)
target_include_directories(eptz_crop PUBLIC src/pipeline)

enable_testing()
add_executable(test_eptz_crop src/tests/test_eptz_crop.cpp)
target_link_libraries(test_eptz_crop PRIVATE eptz_crop)
add_test(NAME eptz_crop COMMAND test_eptz_crop)
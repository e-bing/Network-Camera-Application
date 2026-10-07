cmake_minimum_required(VERSION 3.16)
project(network-camera)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-rtsp-server-1.0)

add_executable(network-camera src/main.cpp)
target_include_directories(network-camera PRIVATE ${GST_INCLUDE_DIRS})
target_link_libraries(network-camera PRIVATE ${GST_LIBRARIES})
target_compile_options(network-camera PRIVATE ${GST_CFLAGS_OTHER})
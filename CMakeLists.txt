cmake_minimum_required(VERSION 3.20)

project(Anim8orX
    VERSION 0.1.0
    DESCRIPTION "Modern C++ and Vulkan spiritual successor to Anim8or"
    LANGUAGES CXX)

add_library(anim8orx_core INTERFACE)
target_include_directories(anim8orx_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(anim8orx_core INTERFACE cxx_std_17)

add_executable(anim8orx_sandbox
    src/main.cpp)
target_link_libraries(anim8orx_sandbox PRIVATE anim8orx_core)

if(MSVC)
    target_compile_options(anim8orx_sandbox PRIVATE /W4 /permissive-)
else()
    target_compile_options(anim8orx_sandbox PRIVATE -Wall -Wextra -Wpedantic)
endif()


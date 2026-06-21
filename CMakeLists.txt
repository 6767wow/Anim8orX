cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(Anim8orX
    VERSION 0.6.0
    DESCRIPTION "Modern C++ and Vulkan spiritual successor to Anim8or"
    LANGUAGES CXX)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(anim8orx_core INTERFACE)
target_include_directories(anim8orx_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(anim8orx_core INTERFACE cxx_std_17)

add_executable(anim8orx_app WIN32
    src/main.cpp)
set_target_properties(anim8orx_app PROPERTIES OUTPUT_NAME "Anim8orX")
target_link_libraries(anim8orx_app PRIVATE anim8orx_core user32 gdi32 shell32 gdiplus comdlg32)

if(MSVC)
    target_compile_options(anim8orx_app PRIVATE /W4 /permissive-)
else()
    target_compile_options(anim8orx_app PRIVATE -Wall -Wextra -Wpedantic)
endif()

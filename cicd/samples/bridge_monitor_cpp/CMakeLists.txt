cmake_minimum_required(VERSION 3.16)
project(shalom_bridge_monitor LANGUAGES CXX)

add_executable(shalom_bridge_monitor main.cpp)
target_compile_features(shalom_bridge_monitor PRIVATE cxx_std_17)
target_include_directories(shalom_bridge_monitor PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../common/protocol/include")

if(MSVC)
    target_compile_options(shalom_bridge_monitor PRIVATE /W4 /permissive-)
else()
    target_compile_options(shalom_bridge_monitor PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Shalom 연동 SDK — Linux 빌드
#
# 이 디렉터리에서 바로 설정한다.
#
#     cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#     cmake --build build
#
# 이 플랫폼 폴더는 전체 SDK 안에서 독립적으로 빌드된다.

cmake_minimum_required(VERSION 3.16)
project(shalom_sdk_linux VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "이 디렉터리는 Linux 용이다. 현재: ${CMAKE_SYSTEM_NAME}")
endif()

add_library(shalom_sdk INTERFACE)
add_library(shalom::sdk ALIAS shalom_sdk)
target_include_directories(shalom_sdk INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_features(shalom_sdk INTERFACE cxx_std_17)

# Linux 는 별도 소켓 라이브러리가 필요 없다. glibc 안에 있다.
find_package(Threads REQUIRED)
target_link_libraries(shalom_sdk INTERFACE Threads::Threads)

add_executable(shalom_monitor monitor.cpp)
target_link_libraries(shalom_monitor PRIVATE shalom::sdk)
target_compile_options(shalom_monitor PRIVATE -Wall -Wextra -Wpedantic)

include(GNUInstallDirs)
install(TARGETS shalom_monitor RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

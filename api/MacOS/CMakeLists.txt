# GTX-A 연동 SDK — macOS 빌드
#
#     cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#     cmake --build build
#
# 유니버설 바이너리가 필요하면:
#
#     cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
#
# 이 폴더 하나만 고객에게 전달하면 된다. 헤더와 예제가 안에 모두 있고
# 바깥을 참조하지 않는다.

cmake_minimum_required(VERSION 3.16)
project(shalom_api_macos VERSION 1.0.0 LANGUAGES CXX)

if(NOT APPLE)
    message(FATAL_ERROR "이 디렉터리는 macOS 용이다. 현재: ${CMAKE_SYSTEM_NAME}")
endif()

# 10.15 부터 C++17 표준 라이브러리가 온전히 들어 있다.
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "최소 macOS 버전" FORCE)
endif()

add_library(shalom_api INTERFACE)
add_library(shalom::api ALIAS shalom_api)
target_include_directories(shalom_api INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_features(shalom_api INTERFACE cxx_std_17)

find_package(Threads REQUIRED)
target_link_libraries(shalom_api INTERFACE Threads::Threads)

add_executable(shalom_monitor monitor.cpp)
target_link_libraries(shalom_monitor PRIVATE shalom::api)
target_compile_options(shalom_monitor PRIVATE -Wall -Wextra -Wpedantic)

include(GNUInstallDirs)
install(TARGETS shalom_monitor RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

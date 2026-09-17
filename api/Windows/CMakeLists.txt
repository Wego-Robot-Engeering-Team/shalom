# GTX-A 연동 SDK — Windows 빌드
#
# 개발자 명령 프롬프트에서:
#
#     cmake -S . -B build -A x64
#     cmake --build build --config Release
#
# 이 폴더 하나만 고객에게 전달하면 된다. 헤더와 예제가 안에 모두 있고
# 바깥을 참조하지 않는다.

cmake_minimum_required(VERSION 3.16)
project(shalom_api_windows VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "이 디렉터리는 Windows 용이다. 현재: ${CMAKE_SYSTEM_NAME}")
endif()

add_library(shalom_api INTERFACE)
add_library(shalom::api ALIAS shalom_api)
target_include_directories(shalom_api INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_features(shalom_api INTERFACE cxx_std_17)

# Winsock. 고객이 따로 적지 않아도 되도록 INTERFACE 로 걸어 둔다.
target_link_libraries(shalom_api INTERFACE ws2_32)

# WSAPoll 은 Vista 부터다. 값을 못 박지 않으면 툴체인마다 다른 것을 골라
# 어떤 SDK 에서는 선언이 보이지 않는다.
target_compile_definitions(shalom_api INTERFACE
    _WIN32_WINNT=0x0600
    WIN32_LEAN_AND_MEAN
    NOMINMAX)

add_executable(shalom_monitor monitor.cpp)
target_link_libraries(shalom_monitor PRIVATE shalom::api)

if(MSVC)
    # /utf-8 이 없으면 MSVC 가 원본 인코딩을 시스템 코드 페이지로 짐작해
    # 한글 문자열을 깨뜨린다.
    target_compile_options(shalom_monitor PRIVATE /W4 /permissive- /utf-8)
else()
    # MinGW
    target_compile_options(shalom_monitor PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)
install(TARGETS shalom_monitor RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

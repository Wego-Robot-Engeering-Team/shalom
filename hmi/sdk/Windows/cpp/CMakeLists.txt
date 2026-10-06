cmake_minimum_required(VERSION 3.16)
project(robot_sdk_windows VERSION 0.3.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "이 디렉터리는 Windows 용이다. 현재: ${CMAKE_SYSTEM_NAME}")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
add_library(robot_sdk SHARED
    src/transport/client.cpp
    src/api/safety.cpp
    src/api/navigation.cpp
    src/api/mission.cpp
    src/api/configuration.cpp
    src/api/inspection.cpp)
add_library(robot_sdk::sdk ALIAS robot_sdk)
target_compile_features(robot_sdk PUBLIC cxx_std_17)
target_compile_definitions(robot_sdk
    PRIVATE ROBOT_SDK_BUILD
    PUBLIC _WIN32_WINNT=0x0600 WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(robot_sdk
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src")
set_target_properties(robot_sdk PROPERTIES VERSION ${PROJECT_VERSION} EXPORT_NAME sdk)
target_link_libraries(robot_sdk PRIVATE ws2_32)

add_executable(robot_monitor examples/monitor.cpp)
target_link_libraries(robot_monitor PRIVATE robot_sdk::sdk)
add_executable(robot_api_example examples/api_example.cpp)
target_link_libraries(robot_api_example PRIVATE robot_sdk::sdk)
if(MSVC)
    foreach(target robot_monitor robot_api_example)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    endforeach()
else()
    foreach(target robot_monitor robot_api_example)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()

install(TARGETS robot_sdk EXPORT RobotSdkTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS robot_monitor robot_api_example RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
    "${CMAKE_CURRENT_LIST_DIR}/include/robot_sdk/api.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/include/robot_sdk/client.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/include/robot_sdk/export.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/include/robot_sdk/types.hpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/robot_sdk")
install(EXPORT RobotSdkTargets NAMESPACE robot_sdk::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/RobotSdk")
configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/RobotSdkConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/RobotSdkConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/RobotSdk")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/RobotSdkConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/RobotSdkConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/RobotSdkConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/RobotSdk")

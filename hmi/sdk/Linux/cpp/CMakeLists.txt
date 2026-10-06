cmake_minimum_required(VERSION 3.16)
project(robot_sdk_linux VERSION 0.3.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "이 디렉터리는 Linux 용이다. 현재: ${CMAKE_SYSTEM_NAME}")
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
target_compile_definitions(robot_sdk PRIVATE ROBOT_SDK_BUILD)
target_include_directories(robot_sdk
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src")
set_target_properties(robot_sdk PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    EXPORT_NAME sdk)

find_package(Threads REQUIRED)
target_link_libraries(robot_sdk PRIVATE Threads::Threads)

add_executable(robot_monitor examples/monitor.cpp)
target_link_libraries(robot_monitor PRIVATE robot_sdk::sdk)
target_compile_options(robot_monitor PRIVATE -Wall -Wextra -Wpedantic)

add_executable(robot_api_example examples/api_example.cpp)
target_link_libraries(robot_api_example PRIVATE robot_sdk::sdk)
target_compile_options(robot_api_example PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(robot_monitor robot_api_example PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")

install(TARGETS robot_sdk EXPORT RobotSdkTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS robot_monitor robot_api_example
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
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
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/RobotSdkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/RobotSdkConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/RobotSdk")

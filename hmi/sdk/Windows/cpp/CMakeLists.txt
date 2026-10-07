cmake_minimum_required(VERSION 3.16)
project(robot_sdk VERSION 0.4.0 LANGUAGES CXX)

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
    SOVERSION 1
    EXPORT_NAME sdk)

find_package(Threads REQUIRED)
find_package(nlohmann_json 3.9 REQUIRED)
target_link_libraries(robot_sdk PRIVATE Threads::Threads nlohmann_json::nlohmann_json)
if(WIN32)
    target_link_libraries(robot_sdk PRIVATE ws2_32)
endif()
if(MSVC)
    target_compile_options(robot_sdk PRIVATE /W4 /utf-8)
else()
    target_compile_options(robot_sdk PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(robot_monitor examples/monitor.cpp)
target_link_libraries(robot_monitor PRIVATE robot_sdk::sdk)

add_executable(robot_api_example examples/api_example.cpp)
target_link_libraries(robot_api_example PRIVATE robot_sdk::sdk)
if(APPLE)
    set_target_properties(robot_monitor robot_api_example PROPERTIES INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
elseif(UNIX)
    set_target_properties(robot_monitor robot_api_example PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(test_sdk tests/test_sdk.cpp)
    target_link_libraries(test_sdk PRIVATE robot_sdk::sdk nlohmann_json::nlohmann_json Threads::Threads)
    target_include_directories(test_sdk PRIVATE src)
    if(WIN32)
        target_link_libraries(test_sdk PRIVATE ws2_32)
    endif()
    add_test(NAME sdk_cpp COMMAND test_sdk)
    set_tests_properties(sdk_cpp PROPERTIES TIMEOUT 15)
endif()

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
install(FILES ../../LICENSE ../../NOTICE ../../VERSION DESTINATION "${CMAKE_INSTALL_DATADIR}/robot_sdk")

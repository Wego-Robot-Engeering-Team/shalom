cmake_minimum_required(VERSION 3.16)
project(shalom_sdk_windows VERSION 0.2.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "이 디렉터리는 Windows 용이다. 현재: ${CMAKE_SYSTEM_NAME}")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
add_library(shalom_sdk SHARED
    src/transport/client.cpp
    src/api/safety.cpp
    src/api/navigation.cpp
    src/api/mission.cpp
    src/api/configuration.cpp
    src/api/inspection.cpp)
add_library(shalom::sdk ALIAS shalom_sdk)
target_compile_features(shalom_sdk PUBLIC cxx_std_17)
target_compile_definitions(shalom_sdk
    PRIVATE SHALOM_SDK_BUILD
    PUBLIC _WIN32_WINNT=0x0600 WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(shalom_sdk
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src")
set_target_properties(shalom_sdk PROPERTIES VERSION ${PROJECT_VERSION} EXPORT_NAME sdk)
target_link_libraries(shalom_sdk PRIVATE ws2_32)

add_executable(shalom_monitor examples/monitor.cpp)
target_link_libraries(shalom_monitor PRIVATE shalom::sdk)
add_executable(shalom_api_example examples/api_example.cpp)
target_link_libraries(shalom_api_example PRIVATE shalom::sdk)
if(MSVC)
    foreach(target shalom_monitor shalom_api_example)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    endforeach()
else()
    foreach(target shalom_monitor shalom_api_example)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()

install(TARGETS shalom_sdk EXPORT ShalomSdkTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS shalom_monitor shalom_api_example RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
    "${CMAKE_CURRENT_LIST_DIR}/include/shalom/api.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/include/shalom/client.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/include/shalom/export.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/include/shalom/types.hpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/shalom")
install(EXPORT ShalomSdkTargets NAMESPACE shalom::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ShalomSdk")
configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/ShalomSdkConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ShalomSdkConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ShalomSdk")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ShalomSdkConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ShalomSdkConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/ShalomSdkConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ShalomSdk")

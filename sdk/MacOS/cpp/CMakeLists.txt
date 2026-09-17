cmake_minimum_required(VERSION 3.16)
project(shalom_sdk_macos VERSION 0.2.0 LANGUAGES CXX)

if(NOT APPLE)
    message(FATAL_ERROR "이 디렉터리는 macOS 용이다. 현재: ${CMAKE_SYSTEM_NAME}")
endif()
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "최소 macOS 버전" FORCE)
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
target_compile_definitions(shalom_sdk PRIVATE SHALOM_SDK_BUILD)
target_include_directories(shalom_sdk
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src")
set_target_properties(shalom_sdk PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    EXPORT_NAME sdk)
find_package(Threads REQUIRED)
target_link_libraries(shalom_sdk PRIVATE Threads::Threads)

add_executable(shalom_monitor examples/monitor.cpp)
target_link_libraries(shalom_monitor PRIVATE shalom::sdk)
target_compile_options(shalom_monitor PRIVATE -Wall -Wextra -Wpedantic)
add_executable(shalom_api_example examples/api_example.cpp)
target_link_libraries(shalom_api_example PRIVATE shalom::sdk)
target_compile_options(shalom_api_example PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(shalom_monitor shalom_api_example PROPERTIES
    INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")

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

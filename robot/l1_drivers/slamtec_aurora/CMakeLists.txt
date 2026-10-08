cmake_minimum_required(VERSION 3.16)
project(slamtec_aurora)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(interfaces REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core)

set(AURORA_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/aurora_ros/src/aurora_remote_public")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  set(AURORA_SDK_ARCH linux_aarch64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
  set(AURORA_SDK_ARCH linux_x86_64)
else()
  message(FATAL_ERROR "Unsupported Aurora SDK architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(AURORA_SDK_LIBRARY "${AURORA_SDK_ROOT}/lib/${AURORA_SDK_ARCH}/libslamtec_aurora_remote_sdk.so")
if(NOT EXISTS "${AURORA_SDK_LIBRARY}")
  message(FATAL_ERROR "Aurora SDK library not found: ${AURORA_SDK_LIBRARY}")
endif()
add_library(aurora_sdk SHARED IMPORTED)
set_target_properties(aurora_sdk PROPERTIES
  IMPORTED_LOCATION "${AURORA_SDK_LIBRARY}"
  IMPORTED_NO_SONAME TRUE)

add_executable(aurora_imaging src/enhanced_imaging_node.cpp)
target_compile_features(aurora_imaging PRIVATE cxx_std_17)
target_include_directories(aurora_imaging PRIVATE "${AURORA_SDK_ROOT}/include" ${OpenCV_INCLUDE_DIRS})
ament_target_dependencies(aurora_imaging rclcpp sensor_msgs interfaces)
target_link_libraries(aurora_imaging aurora_sdk ${OpenCV_LIBRARIES})
set_target_properties(aurora_imaging PROPERTIES INSTALL_RPATH "$ORIGIN/..")
install(TARGETS aurora_imaging DESTINATION lib/${PROJECT_NAME})
install(FILES "${AURORA_SDK_LIBRARY}" DESTINATION lib)

install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME}
  PATTERN "__pycache__" EXCLUDE)

ament_package()

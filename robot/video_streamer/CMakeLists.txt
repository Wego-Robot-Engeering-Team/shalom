cmake_minimum_required(VERSION 3.16)
project(video_streamer)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_srvs REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
  gstreamer-1.0 gstreamer-app-1.0 gstreamer-rtsp-server-1.0)

# 과업지시서 4장이 S/W 성과물을 .so 로 요구한다. 컴포넌트로 만들면 그
# 요구를 만족하면서, realsense2_camera 와 한 컨테이너에 합성해 프레임이
# DDS 를 타지 않게 할 수 있다 — 720p RGB8 한 장이 2.76 MB 라, 루프백
# UDP 로 조각내 보내면 그것만으로 CPU 를 먹는다.
add_library(video_streamer_component SHARED src/video_streamer_node.cpp)
target_include_directories(video_streamer_component PUBLIC include)
target_link_libraries(video_streamer_component PkgConfig::GST)
ament_target_dependencies(video_streamer_component
  rclcpp rclcpp_components sensor_msgs std_srvs)
rclcpp_components_register_node(video_streamer_component
  PLUGIN "video_streamer::VideoStreamerNode"
  EXECUTABLE video_streamer_node)

install(TARGETS video_streamer_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})

ament_package()

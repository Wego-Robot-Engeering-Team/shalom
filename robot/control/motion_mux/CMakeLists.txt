cmake_minimum_required(VERSION 3.16)
project(motion_mux)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)

add_executable(motion_mux_node src/motion_mux_node.cpp)
target_compile_features(motion_mux_node PRIVATE cxx_std_17)
ament_target_dependencies(motion_mux_node rclcpp geometry_msgs)

install(TARGETS motion_mux_node DESTINATION lib/${PROJECT_NAME})
ament_package()

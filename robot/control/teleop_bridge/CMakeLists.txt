cmake_minimum_required(VERSION 3.16)
project(teleop_bridge)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)

add_executable(teleop_bridge_node src/teleop_bridge_node.cpp)
target_compile_features(teleop_bridge_node PRIVATE cxx_std_17)
ament_target_dependencies(teleop_bridge_node rclcpp std_msgs geometry_msgs)

install(TARGETS teleop_bridge_node DESTINATION lib/${PROJECT_NAME})
ament_package()

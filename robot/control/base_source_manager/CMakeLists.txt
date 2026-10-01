cmake_minimum_required(VERSION 3.16)
project(base_source_manager)

find_package(ament_cmake REQUIRED)
find_package(action_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav2_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(shalom_interfaces REQUIRED)
find_package(std_msgs REQUIRED)

add_executable(base_source_manager_node src/base_source_manager_node.cpp)
target_compile_features(base_source_manager_node PRIVATE cxx_std_17)
ament_target_dependencies(base_source_manager_node
  action_msgs geometry_msgs nav2_msgs rclcpp rclcpp_action shalom_interfaces std_msgs)

install(TARGETS base_source_manager_node DESTINATION lib/${PROJECT_NAME})
ament_package()

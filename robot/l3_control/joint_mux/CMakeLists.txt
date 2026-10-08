cmake_minimum_required(VERSION 3.16)
project(joint_mux)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)

add_executable(joint_mux_node src/joint_mux_node.cpp)
target_compile_features(joint_mux_node PRIVATE cxx_std_17)
ament_target_dependencies(joint_mux_node rclcpp sensor_msgs)

install(TARGETS joint_mux_node DESTINATION lib/${PROJECT_NAME})
ament_package()

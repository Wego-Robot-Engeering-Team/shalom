cmake_minimum_required(VERSION 3.16)
project(safety_gate)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(shalom_interfaces REQUIRED)

add_executable(safety_gate_node src/safety_gate_node.cpp)
target_compile_features(safety_gate_node PRIVATE cxx_std_17)
ament_target_dependencies(safety_gate_node rclcpp geometry_msgs sensor_msgs shalom_interfaces)

install(TARGETS safety_gate_node DESTINATION lib/${PROJECT_NAME})
ament_package()

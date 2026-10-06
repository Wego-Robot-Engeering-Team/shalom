cmake_minimum_required(VERSION 3.16)
project(estop_bridge)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(gateway_transport REQUIRED)
find_package(shalom_interfaces REQUIRED)

add_executable(estop_bridge_node src/estop_bridge_node.cpp)
target_compile_features(estop_bridge_node PRIVATE cxx_std_17)
ament_target_dependencies(estop_bridge_node rclcpp gateway_transport shalom_interfaces)
target_link_libraries(estop_bridge_node gateway_transport::gateway_transport)

install(TARGETS estop_bridge_node DESTINATION lib/${PROJECT_NAME})
ament_package()

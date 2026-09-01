cmake_minimum_required(VERSION 3.16)
project(robot_sim_tools)

find_package(ament_cmake REQUIRED)
find_package(Python3 3.12 EXACT REQUIRED COMPONENTS Development)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)

add_executable(ground_filter src/ground_filter.cpp)
ament_target_dependencies(ground_filter rclcpp sensor_msgs)
target_link_libraries(ground_filter Python3::Python)

install(TARGETS ground_filter
  DESTINATION lib/${PROJECT_NAME}
)

install(
  PROGRAMS scripts/drive.py
  DESTINATION lib/${PROJECT_NAME}
  RENAME drive
)

install(
  PROGRAMS scripts/control_gui.py
  DESTINATION lib/${PROJECT_NAME}
  RENAME control_gui
)

install(
  DIRECTORY config launch
  DESTINATION share/${PROJECT_NAME}
)

ament_package()

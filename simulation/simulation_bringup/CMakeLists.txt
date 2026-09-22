cmake_minimum_required(VERSION 3.16)
project(simulation_bringup)

find_package(ament_cmake REQUIRED)
install(DIRECTORY launch config maps DESTINATION share/${PROJECT_NAME})
install(PROGRAMS
  scripts/base_posture_adapter.py
  DESTINATION lib/${PROJECT_NAME}
)
ament_package()

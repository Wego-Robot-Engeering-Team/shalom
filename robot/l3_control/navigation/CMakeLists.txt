cmake_minimum_required(VERSION 3.16)
project(navigation)

find_package(ament_cmake REQUIRED)

install(DIRECTORY launch config behavior_trees rviz licenses
  DESTINATION share/${PROJECT_NAME} PATTERN "__pycache__" EXCLUDE)

ament_package()

cmake_minimum_required(VERSION 3.16)
project(application)

find_package(ament_cmake REQUIRED)

install(PROGRAMS scripts/drive.py DESTINATION lib/${PROJECT_NAME} RENAME drive)
install(PROGRAMS scripts/stop_stack.sh DESTINATION lib/${PROJECT_NAME} RENAME stop_stack)
install(PROGRAMS scripts/control_gui.py DESTINATION lib/${PROJECT_NAME} RENAME control_gui)

install(DIRECTORY config launch maps rviz DESTINATION share/${PROJECT_NAME})

ament_package()

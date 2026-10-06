cmake_minimum_required(VERSION 3.16)
project(robot_bringup)

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)

ament_python_install_package(${PROJECT_NAME})

# 시스템 전체 실행에 필요한 DDS 설정과 launch는 robot_bringup이 소유한다.
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME}
  PATTERN "__pycache__" EXCLUDE)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME robot_layout COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_layout.py)
  set_tests_properties(robot_layout PROPERTIES TIMEOUT 60)
  add_test(NAME launch_composition COMMAND ${Python3_EXECUTABLE} -B -m pytest
    -q -p no:cacheprovider ${CMAKE_CURRENT_SOURCE_DIR}/test/test_launch_composition.py)
  set_tests_properties(launch_composition PROPERTIES TIMEOUT 60)
endif()

ament_package()

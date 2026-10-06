cmake_minimum_required(VERSION 3.16)
project(robot_bringup)

find_package(ament_cmake REQUIRED)

# 시스템 전체 실행에 필요한 DDS 설정과 launch는 robot_bringup이 소유한다.
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})

# B2 주행 설정은 l2_control/navigation/config/에 둔다. navigation은 분야 폴더라
# package.xml을 두지 않으며, robot_bringup이 함께 설치해 런타임 경로를 제공한다.
install(DIRECTORY
  ${CMAKE_CURRENT_SOURCE_DIR}/../../l2_control/navigation/config
  ${CMAKE_CURRENT_SOURCE_DIR}/../../l2_control/navigation/rviz
  DESTINATION share/${PROJECT_NAME}/navigation)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME robot_layout COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_layout.py)
  set_tests_properties(robot_layout PROPERTIES TIMEOUT 60)
endif()

ament_package()

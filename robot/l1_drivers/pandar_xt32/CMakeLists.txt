cmake_minimum_required(VERSION 3.16)
project(pandar_xt32)

find_package(ament_cmake REQUIRED)

# Hesai의 드라이버 구현은 third_party/hesai_lidar_ros2 에 둔다. 이 패키지는
# 그 드라이버를 로봇 공통 점군·TF 인터페이스에 맞추는 얇은 어댑터다.
install(DIRECTORY launch config rviz DESTINATION share/${PROJECT_NAME}
  PATTERN "__pycache__" EXCLUDE)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME xt32_launch COMMAND ${Python3_EXECUTABLE} -B -m pytest -q -p no:cacheprovider
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_xt32_launch.py)
  set_tests_properties(xt32_launch PROPERTIES TIMEOUT 30)
endif()

ament_package()

cmake_minimum_required(VERSION 3.16)
project(simulation_bringup)

find_package(ament_cmake REQUIRED)
install(DIRECTORY launch config maps DESTINATION share/${PROJECT_NAME}
  PATTERN "__pycache__" EXCLUDE)
install(PROGRAMS
  scripts/base_posture_adapter.py
  DESTINATION lib/${PROJECT_NAME}
)
install(FILES README_apriltag.md DESTINATION share/${PROJECT_NAME})
if(BUILD_TESTING)
  find_package(ament_cmake_pytest REQUIRED)
  ament_add_pytest_test(test_apriltag_config test/test_apriltag_config.py)
endif()
ament_package()

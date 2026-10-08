cmake_minimum_required(VERSION 3.16)
project(person_perception)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(interfaces REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc)

add_library(person_cloud_processing STATIC src/person_cloud_processing.cpp)
target_compile_features(person_cloud_processing PUBLIC cxx_std_17)
target_include_directories(person_cloud_processing PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> ${OpenCV_INCLUDE_DIRS})
target_link_libraries(person_cloud_processing PUBLIC ${OpenCV_LIBRARIES})

add_executable(aurora_person_cloud src/person_cloud_node.cpp)
target_compile_features(aurora_person_cloud PRIVATE cxx_std_17)
ament_target_dependencies(aurora_person_cloud rclcpp sensor_msgs interfaces)
target_link_libraries(aurora_person_cloud person_cloud_processing)
install(TARGETS aurora_person_cloud DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config launch rviz DESTINATION share/${PROJECT_NAME}
  PATTERN "__pycache__" EXCLUDE)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  ament_add_gtest(test_person_cloud_processing test/test_person_cloud_processing.cpp)
  target_link_libraries(test_person_cloud_processing person_cloud_processing)
  add_test(NAME person_cloud_stream COMMAND ${Python3_EXECUTABLE} -B
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_person_cloud_stream.py $<TARGET_FILE:aurora_person_cloud>)
  set_tests_properties(person_cloud_stream PROPERTIES TIMEOUT 45)
  add_test(NAME person_launch COMMAND ${Python3_EXECUTABLE} -B -m pytest -q -p no:cacheprovider
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_launch.py)
  set_tests_properties(person_launch PROPERTIES TIMEOUT 30)
endif()

ament_package()

cmake_minimum_required(VERSION 3.16)
project(mission_manager)

find_package(ament_cmake REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav2_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(interfaces REQUIRED)

add_library(mission_logic
  src/mission_state_machine.cpp
  src/bt/status.cpp
  src/bt/nav2_bt.cpp
  src/bt/capture_bt.cpp
  src/bt/stair_bt.cpp
)
target_compile_features(mission_logic PUBLIC cxx_std_17)
target_include_directories(mission_logic
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

add_executable(mission_manager_node src/mission_manager_node.cpp)
target_link_libraries(mission_manager_node mission_logic)
target_compile_features(mission_manager_node PRIVATE cxx_std_17)
ament_target_dependencies(mission_manager_node
  geometry_msgs nav2_msgs nav_msgs rclcpp rclcpp_action interfaces)

if(BUILD_TESTING)
  enable_testing()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME authority_heartbeat COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_authority_heartbeat.py $<TARGET_FILE:mission_manager_node>)
  set_tests_properties(authority_heartbeat PROPERTIES TIMEOUT 25)
  add_executable(test_behavior_trees test/test_behavior_trees.cpp)
  target_link_libraries(test_behavior_trees mission_logic)
  target_compile_features(test_behavior_trees PRIVATE cxx_std_17)
  add_test(NAME behavior_trees COMMAND test_behavior_trees)

  add_executable(test_mission_state_machine test/test_mission_state_machine.cpp)
  target_link_libraries(test_mission_state_machine mission_logic)
  target_compile_features(test_mission_state_machine PRIVATE cxx_std_17)
  add_test(NAME mission_state_machine COMMAND test_mission_state_machine)
endif()

install(
  TARGETS mission_logic mission_manager_node
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY include/ DESTINATION include)
install(FILES src/bt/navigate_to_pose_w_replanning_and_recovery.xml
  DESTINATION share/${PROJECT_NAME}/behavior_trees)
install(FILES src/bt/LICENSE-APACHE-2.0.txt
  DESTINATION share/${PROJECT_NAME}/licenses)

ament_export_include_directories(include)
ament_export_libraries(mission_logic)
ament_export_dependencies(interfaces)
ament_package()

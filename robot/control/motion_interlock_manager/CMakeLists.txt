cmake_minimum_required(VERSION 3.16)
project(motion_interlock_manager)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(shalom_interfaces REQUIRED)

add_library(motion_interlock_logic src/motion_interlock.cpp)
target_compile_features(motion_interlock_logic PUBLIC cxx_std_17)
target_include_directories(motion_interlock_logic PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

add_library(base_stop_detector src/base_stop_detector.cpp)
target_compile_features(base_stop_detector PUBLIC cxx_std_17)
target_include_directories(base_stop_detector PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

add_executable(motion_interlock_manager_node src/motion_interlock_manager_node.cpp)
target_link_libraries(motion_interlock_manager_node motion_interlock_logic)
ament_target_dependencies(motion_interlock_manager_node rclcpp shalom_interfaces)

add_executable(base_motion_monitor_node src/base_motion_monitor_node.cpp)
target_link_libraries(base_motion_monitor_node base_stop_detector)
ament_target_dependencies(base_motion_monitor_node
  rclcpp geometry_msgs nav_msgs shalom_interfaces)

if(BUILD_TESTING)
  enable_testing()
  add_executable(test_motion_interlock test/test_motion_interlock.cpp)
  target_link_libraries(test_motion_interlock motion_interlock_logic)
  target_compile_features(test_motion_interlock PRIVATE cxx_std_17)
  add_test(NAME motion_interlock COMMAND test_motion_interlock)

  add_executable(test_base_stop_detector test/test_base_stop_detector.cpp)
  target_link_libraries(test_base_stop_detector base_stop_detector)
  target_compile_features(test_base_stop_detector PRIVATE cxx_std_17)
  add_test(NAME base_stop_detector COMMAND test_base_stop_detector)
endif()

install(TARGETS
  motion_interlock_logic
  base_stop_detector
  motion_interlock_manager_node
  base_motion_monitor_node
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)

ament_export_include_directories(include)
ament_export_libraries(motion_interlock_logic base_stop_detector)
ament_package()

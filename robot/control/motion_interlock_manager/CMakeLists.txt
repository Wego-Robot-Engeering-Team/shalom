cmake_minimum_required(VERSION 3.16)
project(motion_interlock_manager)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

add_library(motion_interlock_logic src/motion_interlock.cpp)
target_compile_features(motion_interlock_logic PUBLIC cxx_std_17)
target_include_directories(motion_interlock_logic PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

add_executable(motion_interlock_manager_node src/motion_interlock_manager_node.cpp)
target_link_libraries(motion_interlock_manager_node motion_interlock_logic)
ament_target_dependencies(motion_interlock_manager_node rclcpp std_msgs)

if(BUILD_TESTING)
  enable_testing()
  add_executable(test_motion_interlock test/test_motion_interlock.cpp)
  target_link_libraries(test_motion_interlock motion_interlock_logic)
  target_compile_features(test_motion_interlock PRIVATE cxx_std_17)
  add_test(NAME motion_interlock COMMAND test_motion_interlock)
endif()

install(TARGETS motion_interlock_logic motion_interlock_manager_node
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)

ament_export_include_directories(include)
ament_export_libraries(motion_interlock_logic)
ament_package()

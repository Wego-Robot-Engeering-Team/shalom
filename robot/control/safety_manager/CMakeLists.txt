cmake_minimum_required(VERSION 3.16)
project(safety_manager)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

add_library(safety_logic src/safety_fsm.cpp)
target_compile_features(safety_logic PUBLIC cxx_std_17)
target_include_directories(safety_logic PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

add_executable(safety_manager_node src/safety_manager_node.cpp)
target_link_libraries(safety_manager_node safety_logic)
ament_target_dependencies(safety_manager_node rclcpp std_msgs)

if(BUILD_TESTING)
  enable_testing()
  add_executable(test_safety_logic test/test_safety_logic.cpp)
  target_link_libraries(test_safety_logic safety_logic)
  target_compile_features(test_safety_logic PRIVATE cxx_std_17)
  add_test(NAME safety_logic COMMAND test_safety_logic)
endif()

install(TARGETS safety_logic safety_manager_node
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)

ament_export_include_directories(include)
ament_export_libraries(safety_logic)
ament_package()

cmake_minimum_required(VERSION 3.16)
project(mission_manager)

find_package(ament_cmake REQUIRED)

add_library(mission_logic
  src/mission_fsm.cpp
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

add_executable(mission_manager_demo src/mission_manager_demo.cpp)
target_link_libraries(mission_manager_demo mission_logic)
target_compile_features(mission_manager_demo PRIVATE cxx_std_17)

if(BUILD_TESTING)
  enable_testing()
  add_executable(test_mission_logic test/test_mission_logic.cpp)
  target_link_libraries(test_mission_logic mission_logic)
  target_compile_features(test_mission_logic PRIVATE cxx_std_17)
  add_test(NAME mission_logic COMMAND test_mission_logic)
endif()

install(
  TARGETS mission_logic mission_manager_demo
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY include/ DESTINATION include)

ament_export_include_directories(include)
ament_export_libraries(mission_logic)
ament_package()

cmake_minimum_required(VERSION 3.16)
project(vectornav_vn100)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)

add_executable(vn100_imu_adapter src/imu_adapter.cpp)
ament_target_dependencies(vn100_imu_adapter rclcpp sensor_msgs)

install(TARGETS vn100_imu_adapter DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})

ament_package()

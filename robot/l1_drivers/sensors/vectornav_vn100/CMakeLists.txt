cmake_minimum_required(VERSION 3.16)
project(vectornav_vn100)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(visualization_msgs REQUIRED)

add_executable(vn100_imu_adapter src/imu_adapter.cpp)
ament_target_dependencies(vn100_imu_adapter rclcpp sensor_msgs)

add_executable(vn100_attitude_visualizer src/attitude_visualizer.cpp)
target_compile_features(vn100_attitude_visualizer PRIVATE cxx_std_17)
ament_target_dependencies(vn100_attitude_visualizer rclcpp sensor_msgs visualization_msgs)

install(TARGETS vn100_imu_adapter vn100_attitude_visualizer DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config launch rviz DESTINATION share/${PROJECT_NAME})

ament_package()

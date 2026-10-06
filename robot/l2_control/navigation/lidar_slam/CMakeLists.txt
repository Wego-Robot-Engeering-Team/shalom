cmake_minimum_required(VERSION 3.16)
project(lidar_slam)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)

add_executable(ground_filter src/ground_filter.cpp)
ament_target_dependencies(ground_filter rclcpp sensor_msgs)
install(TARGETS ground_filter DESTINATION lib/${PROJECT_NAME})

# 설정은 부르는 쪽이 소유한다 (navigation/config/). 이 패키지는 코드와
# launch 만 낸다.
install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})

ament_package()

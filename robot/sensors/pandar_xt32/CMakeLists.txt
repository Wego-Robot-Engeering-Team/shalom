cmake_minimum_required(VERSION 3.16)
project(pandar_xt32)

find_package(ament_cmake REQUIRED)

# Hesai의 드라이버 구현은 third_party/hesai_lidar_ros2 에 둔다. 이 패키지는
# 그 드라이버를 로봇 공통 점군·TF 인터페이스에 맞추는 얇은 어댑터다.
install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})

ament_package()

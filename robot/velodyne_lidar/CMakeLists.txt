cmake_minimum_required(VERSION 3.16)
project(velodyne_lidar)

find_package(ament_cmake REQUIRED)

# 런치뿐이다. 드라이버는 velodyne 패키지가 갖고 있고, 이 패키지가 하는 일은
# 그것을 B2 의 토픽·좌표계에 맞춰 끼우는 것이다.
install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})

ament_package()

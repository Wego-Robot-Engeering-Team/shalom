cmake_minimum_required(VERSION 3.16)
project(bringup)

find_package(ament_cmake REQUIRED)

# 로봇 없이 스택을 세울 때 쓰는 정지 오도메트리.
install(PROGRAMS scripts/bench_odom.py
        DESTINATION lib/${PROJECT_NAME} RENAME bench_odom)

# 시스템 전체 실행에 필요한 DDS 설정과 launch는 bringup이 소유한다.
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})

# B2 주행 설정은 navigation/config/에 둔다. navigation은 분야 폴더라
# package.xml을 두지 않으며, bringup이 함께 설치해 런타임 경로를 제공한다.
install(DIRECTORY
  ${CMAKE_CURRENT_SOURCE_DIR}/../navigation/config
  ${CMAKE_CURRENT_SOURCE_DIR}/../navigation/maps
  ${CMAKE_CURRENT_SOURCE_DIR}/../navigation/rviz
  DESTINATION share/${PROJECT_NAME}/navigation)

ament_package()

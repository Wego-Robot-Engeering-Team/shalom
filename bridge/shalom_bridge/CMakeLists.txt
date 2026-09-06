cmake_minimum_required(VERSION 3.16)
project(shalom_bridge)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(bridge_node
    src/main.cpp
    src/bridge_node.cpp
    src/envelope.cpp
    src/tcp_server.cpp
)

# 프레이밍은 관제와 공유하는 헤더 하나에서 나온다. 같은 바이트 배치를 두 번
# 구현하면 언젠가 어긋나고, 그 어긋남은 빌드가 아니라 현장에서 드러난다.
target_include_directories(bridge_node PRIVATE
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../protocol/include
)

ament_target_dependencies(bridge_node
    rclcpp std_msgs sensor_msgs geometry_msgs nav_msgs
    tf2 tf2_ros tf2_geometry_msgs
)
target_link_libraries(bridge_node nlohmann_json::nlohmann_json)

install(TARGETS bridge_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})

ament_package()

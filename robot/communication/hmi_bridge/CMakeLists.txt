cmake_minimum_required(VERSION 3.16)
project(hmi_bridge)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(nav2_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(nlohmann_json REQUIRED)
# 점유격자를 PNG 로 눌러 보낸다 (map/occupancy).
find_package(ZLIB REQUIRED)

add_executable(bridge_node
    src/main.cpp
    src/bridge_node.cpp
    src/envelope.cpp
    src/tcp_server.cpp
)

# 프레이밍은 관제와 공유하는 헤더 하나에서 나온다. 같은 바이트 배치를 두 번
# 구현하면 언젠가 어긋나고, 그 어긋남은 빌드가 아니라 현장에서 드러난다.
target_link_libraries(bridge_node ZLIB::ZLIB)

target_include_directories(bridge_node PRIVATE
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/protocol/include
)

ament_target_dependencies(bridge_node
    rclcpp rclcpp_action std_msgs sensor_msgs geometry_msgs nav_msgs nav2_msgs
    tf2 tf2_ros tf2_geometry_msgs
)
target_link_libraries(bridge_node nlohmann_json::nlohmann_json)

install(TARGETS bridge_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})

# ---------------------------------------------------------------------------
# 시험
#
# 소켓·스레드·프레이밍이 얽히는 전송 계층이 이 노드에서 실제로 위험한
# 곳이다. tcp_server 는 ROS 를 쓰지 않으므로 rclcpp 없이 그대로 붙는다.
#
# 예전에는 별도 CMake 프로젝트(standalone/)로 두었다. "ROS 가 없는 장비에서도
# 검증할 수 있게" 가 이유였는데 그런 장비가 이제 없고, colcon test 가 돌리지
# 않으니 아무도 안 돌렸다 — 이 패키지에 시험이 하나뿐이었던 것도 그래서다.
if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    add_executable(test_tcp_server
        test/test_tcp_server.cpp
        src/tcp_server.cpp)
    target_include_directories(test_tcp_server PRIVATE include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/protocol/include)
    target_link_libraries(test_tcp_server PRIVATE Threads::Threads)
    add_test(NAME tcp_server COMMAND test_tcp_server)
endif()

ament_package()

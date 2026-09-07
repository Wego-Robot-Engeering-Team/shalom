cmake_minimum_required(VERSION 3.16)
project(shalom_bridge_transport_tests CXX)

# 전송 계층만 ROS 2 없이 빌드한다.
#
# 브릿지 노드 전체는 rclcpp 가 있어야 빌드되지만, 소켓·스레드·프레이밍이
# 얽히는 이 부분이 실제 위험이 있는 곳이다. ROS 2 가 깔린 장비가 없어도
# 여기만은 검증할 수 있어야 한다.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)

add_executable(test_tcp_server
    ../shalom_bridge/src/tcp_server.cpp
    test_tcp_server.cpp
)
target_include_directories(test_tcp_server PRIVATE
    ../shalom_bridge/include
    ../../../common/protocol/include
)
target_link_libraries(test_tcp_server PRIVATE Threads::Threads)

enable_testing()
add_test(NAME tcp_server COMMAND test_tcp_server)

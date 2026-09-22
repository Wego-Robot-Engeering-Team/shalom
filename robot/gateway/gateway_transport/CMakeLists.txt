cmake_minimum_required(VERSION 3.16)
project(gateway_transport)

find_package(ament_cmake REQUIRED)
find_package(nlohmann_json REQUIRED)

add_library(gateway_transport
    src/envelope.cpp
    src/tcp_server.cpp
)
target_compile_features(gateway_transport PUBLIC cxx_std_17)
target_include_directories(gateway_transport PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../../common/protocol/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(gateway_transport PUBLIC nlohmann_json::nlohmann_json)

install(DIRECTORY include/ DESTINATION include)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/protocol/include/inspection/framing.hpp
    DESTINATION include/inspection)
install(TARGETS gateway_transport EXPORT gateway_transport_targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

ament_export_include_directories(include)
ament_export_targets(gateway_transport_targets HAS_LIBRARY_TARGET)
ament_export_dependencies(nlohmann_json)
ament_package()

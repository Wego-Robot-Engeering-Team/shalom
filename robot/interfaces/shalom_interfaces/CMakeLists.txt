cmake_minimum_required(VERSION 3.16)
project(shalom_interfaces)

find_package(ament_cmake REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/MissionPlan.msg"
  "msg/MissionState.msg"
  "msg/MissionWaypoint.msg"
  "msg/MotionAuthority.msg"
  "msg/MotionStopped.msg"
  "msg/SafetyHeartbeat.msg"
  "msg/SafetyEvent.msg"
  "msg/SafetyState.msg"
  "srv/AuthorityRequest.srv"
  "srv/ConfigureMission.srv"
  "srv/MissionControl.srv"
  "srv/SafetyCommand.srv"
  DEPENDENCIES builtin_interfaces geometry_msgs
)

ament_export_dependencies(rosidl_default_runtime)
ament_package()

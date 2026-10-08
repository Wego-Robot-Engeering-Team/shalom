# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""HMI·E-Stop TCP 실행. 안전 관리자는 L4 system launch가 실행한다."""

from pathlib import Path

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.actions import Node


def generate_launch_description():
    default_config = str(
        Path(get_package_share_directory("hmi_bridge")) / "config" / "bridge.yaml"
    )

    config_arg = DeclareLaunchArgument(
        "config",
        default_value=default_config,
        description="브릿지 파라미터 파일 경로",
    )
    estop_port_arg = DeclareLaunchArgument(
        "estop_port", default_value="9091",
        description="E-Stop 전용 TCP 포트. 일반 HMI 포트와 분리한다.",
    )

    # 시뮬레이터와 함께 돌 때는 /clock 을 따라야 한다. 브릿지가 벽시계를
    # 쓰면 TF 조회가 시뮬 시각과 어긋나 위치가 나가지 않는다. 실기에서는
    # false 로 둔다 — true 로 두면 오지 않는 /clock 을 기다리며 멈춘다.
    sim_time_arg = DeclareLaunchArgument(
        "use_sim_time",
        default_value="false",
        description="시뮬레이터와 함께 돌 때만 true",
    )

    # 상위 실행 구성이 로봇 ID를 결정하고 두 TCP 노드에 동일하게 전달한다.
    robot_id_arg = DeclareLaunchArgument(
        "robot_id", description="상위 bringup에서 전달하는 로봇 식별자")
    robot_name_arg = DeclareLaunchArgument(
        "robot_name", default_value="1호기",
        description="화면에 보일 이름")
    maps_dir_arg = DeclareLaunchArgument(
        "maps_dir", default_value="/var/lib/shalom/maps",
        description="지도 번들과 지도별 상태가 있는 로봇 로컬 디렉터리")
    robot_data_dir_arg = DeclareLaunchArgument(
        "robot_data_dir", default_value="/var/lib/shalom",
        description="지도와 무관한 로봇 운용 데이터 디렉터리")
    initial_map_arg = DeclareLaunchArgument(
        "initial_map", default_value="",
        description="시작 지도 map.yaml 절대 경로 또는 빈 값. map_server와 같은 지도를 사용해야 한다.")

    # 로봇이 실제로 내보내는 이름에 붙인다. 노드 안에서는 상대 이름을 쓰므로
    # 다른 스택에 얹을 때는 여기만 고치면 된다.
    #
    # 시뮬레이터(b2_mujoco)와 실기 드라이버(b2_base)가 같은 토픽에 같은
    # 메시지를 낸다. 이 노드도, 관제도 둘을 구분하지 못한다 — 그게 목적이다.
    remaps = [
        ("battery_state", "/b2/battery_state"),
        ("fr3/joint_states", "/fr3/joint_states"),
    ]

    bridge = Node(
        package="hmi_bridge",
        executable="hmi_bridge_node",
        name="hmi_bridge",
        output="screen",
        remappings=remaps,
        parameters=[LaunchConfiguration("config"),
                    {"use_sim_time": LaunchConfiguration("use_sim_time"),
                     "robot_id": LaunchConfiguration("robot_id"),
                     "robot_name": LaunchConfiguration("robot_name"),
                     "maps_dir": LaunchConfiguration("maps_dir"),
                     "robot_data_dir": LaunchConfiguration("robot_data_dir"),
                     "initial_map": LaunchConfiguration("initial_map")}],
        # 브릿지가 죽으면 생존 신호가 끊기고 안전 노드가 로봇을 정지시킨다.
        # 그 뒤 자동으로 다시 올라와 관제가 재연결할 수 있게 한다.
        respawn=True,
        respawn_delay=2.0,
    )

    estop_bridge = Node(
        package="estop_bridge",
        executable="estop_bridge_node",
        name="estop_bridge",
        output="screen",
        parameters=[{
            "port": ParameterValue(LaunchConfiguration("estop_port"), value_type=int),
            "robot_id": LaunchConfiguration("robot_id"),
            "heartbeat_timeout_ms": 1000,
            "use_sim_time": LaunchConfiguration("use_sim_time"),
        }],
        respawn=True,
        respawn_delay=2.0,
    )

    return LaunchDescription(
        [config_arg, estop_port_arg, sim_time_arg, robot_id_arg, robot_name_arg,
         maps_dir_arg, robot_data_dir_arg, initial_map_arg, estop_bridge, bridge])

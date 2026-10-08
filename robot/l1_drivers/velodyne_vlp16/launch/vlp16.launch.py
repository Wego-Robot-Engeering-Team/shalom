# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Run the test VLP-16 adapter with optional RViz; preserve robot topic and TF."""

from launch import LaunchDescription
from launch.actions import (DeclareLaunchArgument, GroupAction,
                            IncludeLaunchDescription)
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node, SetRemap
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    velodyne = FindPackageShare("velodyne")
    share = FindPackageShare("velodyne_vlp16")

    # 드라이버가 처음부터 B2 의 토픽 이름으로 내게 한다. 중계 노드를 하나
    # 더 두면 그만큼 지연이 붙고, topic_tools 의존도 생긴다.
    #
    # 스택은 /b2/points 를 b2/lidar_link 좌표계로 기다린다. 그 사실을 이
    # 파일에서만 흡수하고, 나머지는 어느 라이다인지 모른 채로 둔다.
    driver = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution(
                [velodyne, "launch", "velodyne-all-nodes-VLP16-launch.py"])),
    )

    remap = SetRemap(src="/velodyne_points", dst=LaunchConfiguration("points_topic"))

    # 어디에 얹혀 있는지. B2 의 라이다 자리와 같은 값을 쓰면 인식 설정을
    # 그대로 둘 수 있다. 실제로 다른 데 올려 두었으면 이 인자로 고친다.
    mount = Node(
        package="tf2_ros",
        executable="static_transform_publisher",
        name="vlp16_mount",
        arguments=[
            "--x", LaunchConfiguration("x"),
            "--y", LaunchConfiguration("y"),
            "--z", LaunchConfiguration("z"),
            "--yaw", LaunchConfiguration("yaw"),
            "--frame-id", LaunchConfiguration("base_frame"),
            "--child-frame-id", "velodyne",
        ],
        output="screen",
        condition=IfCondition(LaunchConfiguration("start_sensor")),
    )

    # velodyne 좌표계를 B2 라이다 이름에도 걸어 둔다. 점군은 velodyne 으로
    # 오는데 인식 설정은 b2/lidar_link 를 가리키므로, 둘을 같은 자리로
    # 묶어야 TF 조회가 성립한다.
    alias = Node(
        package="tf2_ros",
        executable="static_transform_publisher",
        name="vlp16_frame_alias",
        arguments=["--frame-id", "velodyne", "--child-frame-id", "b2/lidar_link"],
        output="screen",
        condition=IfCondition(LaunchConfiguration("start_sensor")),
    )
    rviz = Node(
        package="rviz2",
        executable="rviz2",
        name="vlp16_rviz",
        arguments=["-d", PathJoinSubstitution([share, "rviz", "vlp16.rviz"]),
                   "-f", LaunchConfiguration("fixed_frame")],
        remappings=[("/b2/points", LaunchConfiguration("points_topic"))],
        condition=IfCondition(LaunchConfiguration("rviz")),
        output="screen",
    )

    return LaunchDescription([
        DeclareLaunchArgument("points_topic", default_value="/b2/points",
                              description="스택이 기다리는 점군 토픽"),
        DeclareLaunchArgument("start_sensor", default_value="true",
                              description="Start the driver, point-cloud conversion and mount TF."),
        DeclareLaunchArgument("rviz", default_value="false",
                              description="Open the VLP-16 point-cloud viewer."),
        DeclareLaunchArgument("fixed_frame", default_value="velodyne",
                              description="RViz display frame."),
        DeclareLaunchArgument("base_frame", default_value="base_link"),
        DeclareLaunchArgument("x", default_value="0.34218",
                              description="B2 내장 라이다와 같은 자리. 실제 장착에 맞춰 고칠 것."),
        DeclareLaunchArgument("y", default_value="0.0"),
        DeclareLaunchArgument("z", default_value="0.20"),
        DeclareLaunchArgument("yaw", default_value="0.0"),
        GroupAction([remap, driver], scoped=True,
                    condition=IfCondition(LaunchConfiguration("start_sensor"))),
        mount, alias, rviz,
    ])

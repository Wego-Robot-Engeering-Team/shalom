"""임시 시험용 VLP-16 을 B2 내장 라이다 자리에 끼운다.

B2 의 라이다가 없는 자리에서 인식·SLAM 을 시험하기 위한 것이다. 스택의
나머지는 이 라이다가 무엇인지 몰라야 한다 — 그래서 토픽 이름과 좌표계를
B2 가 내는 것과 같게 맞춘다. bringup 의 pointcloud_topic 을 건드리지 않는
이유도 그것이다.

    ros2 launch bringup bringup.launch.py robot:=real lidar:=vlp16

장치 설정
---------
VLP-16 은 192.168.1.201 에서 255.255.255.255 로 브로드캐스트한다. 받는
쪽 인터페이스에 같은 대역의 주소가 없으면 드라이버가 한 장도 못 받고
"Velodyne poll() timeout" 만 찍는다 — 패킷은 tcpdump 에 그대로 보이므로
케이블 문제로 오해하기 쉽다.

    sudo nmcli con add type ethernet ifname <iface> con-name vlp16 \\
        ipv4.method manual ipv4.addresses 192.168.1.100/24 ipv4.never-default yes

never-default 는 이 유선이 기본 경로를 가져가지 않게 한다. 가져가면 무선
쪽 관제 연결이 끊긴다.
"""

from launch import LaunchDescription
from launch.actions import (DeclareLaunchArgument, GroupAction,
                            IncludeLaunchDescription)
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node, SetRemap
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    velodyne = FindPackageShare("velodyne")

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
    )

    return LaunchDescription([
        DeclareLaunchArgument("points_topic", default_value="/b2/points",
                              description="스택이 기다리는 점군 토픽"),
        DeclareLaunchArgument("base_frame", default_value="base_link"),
        DeclareLaunchArgument("x", default_value="0.34218",
                              description="B2 내장 라이다와 같은 자리. 실제 장착에 맞춰 고칠 것."),
        DeclareLaunchArgument("y", default_value="0.0"),
        DeclareLaunchArgument("z", default_value="0.20"),
        DeclareLaunchArgument("yaw", default_value="0.0"),
        GroupAction([remap, driver]), mount, alias,
    ])

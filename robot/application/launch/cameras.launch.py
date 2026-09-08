"""RealSense 카메라 한 대를 정해진 역할로 띄운다.

과업지시서 하드웨어 구성이 카메라 셋을 요구한다 — 본체 하나, 로봇암
끝단에 2D·3D 하나씩. D455 는 컬러와 깊이를 함께 내므로 암 끝단에서는
한 대가 2D 와 3D 를 겸한다.

    ros2 launch application cameras.launch.py role:=arm
    ros2 launch application cameras.launch.py role:=body serial:=012345678901

역할을 인자로 받는 이유는, 어느 카메라를 어디에 달지가 아직 정해지지
않았기 때문이다. 장착이 확정되면 b2_navigation.launch.py 에서 필요한
역할만 켜면 된다.

토픽 이름은 shalom_bridge 의 bridge.yaml 이 이미 기다리고 있는 것에
맞춘다. 이름이 어긋나면 화면의 "센서 상태" 에 카메라가 영영 "신호 없음"
으로 남는데, 그림은 정상으로 보이므로 눈으로는 늦게 발견된다.
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


# 역할별 토픽 이름. bridge.yaml 의 sensor.<id>.topic 과 같아야 한다.
ROLES = {
    "body": {
        "namespace": "b2/camera",
        "remap": [("~/color/image_raw", "/b2/camera/image_raw"),
                  ("~/color/camera_info", "/b2/camera/camera_info")],
        # 본체 카메라는 사람·장애물을 보는 눈이다(과업지시서 2.2.5).
        # 포인트클라우드까지 낼 이유가 없어 깊이는 끈다.
        "overrides": {"enable_depth": False,
                      "pointcloud.enable": False,
                      "align_depth.enable": False},
    },
    "arm": {
        "namespace": "fr3/camera",
        "remap": [("~/color/image_raw", "/fr3/camera_2d/image_raw"),
                  ("~/color/camera_info", "/fr3/camera_2d/camera_info"),
                  ("~/depth/color/points", "/fr3/camera_3d/points"),
                  ("~/aligned_depth_to_color/image_raw",
                   "/fr3/camera_3d/image_raw")],
        "overrides": {},
    },
}


def _camera(context, *_args, **_kwargs):
    role = LaunchConfiguration("role").perform(context)
    if role not in ROLES:
        raise RuntimeError(
            f"role 은 {'|'.join(ROLES)} 중 하나여야 한다 (받은 값: {role!r})")

    spec = ROLES[role]
    serial = LaunchConfiguration("serial").perform(context)
    frame_prefix = LaunchConfiguration("frame_prefix").perform(context)

    params = [PathJoinSubstitution(
        [FindPackageShare("application"), "config", "realsense_d455.yaml"])]

    overrides = dict(spec["overrides"])
    # 시리얼을 주면 그 카메라만 연다. 두 대를 함께 달면 이것 없이는
    # 어느 쪽이 열릴지 실행할 때마다 달라진다.
    if serial:
        overrides["serial_no"] = serial
    overrides["camera_name"] = role
    # TF 이름이 겹치면 두 카메라의 좌표계가 서로를 덮어쓴다.
    overrides["frame_id"] = f"{frame_prefix}{role}_camera_link"
    params.append(overrides)

    return [Node(
        package="realsense2_camera",
        executable="realsense2_camera_node",
        name=f"{role}_camera",
        namespace=spec["namespace"],
        parameters=params,
        remappings=spec["remap"],
        output="screen",
        # 카메라가 빠져도 나머지 스택은 살아 있어야 한다. USB 를 다시 꽂는
        # 동안 자율주행이 함께 죽으면 현장에서 곤란하다.
        respawn=True,
        respawn_delay=4.0,
    )]


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument(
            "role", default_value="arm",
            description="body = 본체 카메라, arm = 로봇암 끝단(2D+3D)"),
        DeclareLaunchArgument(
            "serial", default_value="",
            description="장치 시리얼. 두 대 이상 달았으면 반드시 지정한다."),
        DeclareLaunchArgument(
            "frame_prefix", default_value="",
            description="TF 이름 앞에 붙일 접두사"),
        OpaqueFunction(function=_camera),
    ])

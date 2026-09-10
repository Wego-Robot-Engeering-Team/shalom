"""RealSense D455와 범용 영상 송신기를 한 컨테이너에서 실행한다.

이 패키지는 D455의 역할·시리얼·토픽 규약을 소유한다. H.264/RTSP 구현은
video_streamer 패키지에 있으며, 여기서는 두 컴포넌트를 합성해 intra-process
전달을 유지한다.

    ros2 launch realsense_d455 d455_stream.launch.py \\
        role:=arm serial:=213522250834 autostart:=false
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import ComposableNodeContainer
from launch_ros.descriptions import ComposableNode
from launch_ros.substitutions import FindPackageShare


# 이 이름은 hmi_bridge/config/bridge.yaml의 센서 토픽과 한 쌍이다.
ROLES = {
    "body": {
        "namespace": "b2/camera",
        "image_topic": "/b2/camera/image_raw",
        "remappings": [
            ("~/color/image_raw", "/b2/camera/image_raw"),
            ("~/color/camera_info", "/b2/camera/camera_info"),
        ],
        "overrides": {
            "enable_depth": False,
            "pointcloud__neon_.enable": False,
            "align_depth.enable": False,
        },
    },
    "arm": {
        "namespace": "fr3/camera",
        "image_topic": "/fr3/camera_2d/image_raw",
        "remappings": [
            ("~/color/image_raw", "/fr3/camera_2d/image_raw"),
            ("~/color/camera_info", "/fr3/camera_2d/camera_info"),
            ("~/depth/color/points", "/fr3/camera_3d/points"),
            ("~/aligned_depth_to_color/image_raw", "/fr3/camera_3d/image_raw"),
        ],
        "overrides": {},
    },
}


def _container(context, *_args, **_kwargs):
    role = LaunchConfiguration("role").perform(context)
    if role not in ROLES:
        raise RuntimeError(
            f"role은 {'|'.join(ROLES)} 중 하나여야 한다 (받은 값: {role!r})")

    spec = ROLES[role]
    serial = LaunchConfiguration("serial").perform(context)
    frame_prefix = LaunchConfiguration("frame_prefix").perform(context)
    camera_package = FindPackageShare("realsense_d455")
    streamer_package = FindPackageShare("video_streamer")

    camera_overrides = dict(spec["overrides"])
    if serial:
        camera_overrides["serial_no"] = serial
    camera_overrides["camera_name"] = role
    camera_overrides["frame_id"] = f"{frame_prefix}{role}_camera_link"

    return [ComposableNodeContainer(
        name=f"{role}_camera_container",
        namespace="",
        package="rclcpp_components",
        executable="component_container_mt",
        composable_node_descriptions=[
            ComposableNode(
                package="realsense2_camera",
                plugin="realsense2_camera::RealSenseNodeFactory",
                name=f"{role}_camera",
                namespace=spec["namespace"],
                parameters=[
                    PathJoinSubstitution([camera_package, "config", "d455.yaml"]),
                    camera_overrides,
                ],
                remappings=spec["remappings"],
                extra_arguments=[{"use_intra_process_comms": True}],
            ),
            ComposableNode(
                package="video_streamer",
                plugin="video_streamer::VideoStreamerNode",
                name="video_streamer",
                namespace=spec["namespace"],
                parameters=[
                    PathJoinSubstitution([streamer_package, "config", "stream.yaml"]),
                    {
                        "image_topic": spec["image_topic"],
                        "encoder": LaunchConfiguration("encoder"),
                        "bind_address": LaunchConfiguration("bind_address"),
                        "autostart": LaunchConfiguration("autostart"),
                    },
                ],
                extra_arguments=[{"use_intra_process_comms": True}],
            ),
        ],
        output="screen",
    )]


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument(
            "role", default_value="arm",
            description="body = 본체 카메라, arm = 로봇암 끝단 D455 (2D+3D)"),
        DeclareLaunchArgument(
            "serial", default_value="",
            description="장치 시리얼. 두 대 이상이면 반드시 지정한다."),
        DeclareLaunchArgument(
            "frame_prefix", default_value="",
            description="카메라 TF 이름 앞에 붙일 접두사"),
        DeclareLaunchArgument(
            "encoder", default_value="nvv4l2h264enc",
            description="AGX: nvv4l2h264enc. Nano 시험 시 autostart:=false."),
        DeclareLaunchArgument(
            "bind_address", default_value="127.0.0.1",
            description="RTSP가 들을 로봇 내부망 주소"),
        DeclareLaunchArgument(
            "autostart", default_value="true",
            description="첫 프레임 뒤 RTSP 송신을 시작할지"),
        OpaqueFunction(function=_container),
    ])

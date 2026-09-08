"""카메라와 영상 송신을 한 컨테이너에 합성해 띄운다.

두 노드를 따로 띄우면 컬러 프레임이 DDS 를 탄다. 720p RGB8 한 장이
2.76 MB 라 CycloneDDS 가 루프백 UDP 로 수백 조각을 내는데, 실제로 그
때문에 BEST_EFFORT 구독에 한 장도 들어오지 않았다. 같은 컨테이너에
합성하면 intra-process 로 넘어가 그 경로를 통째로 건너뛴다.

컴포넌트로 만든 덕에 산출물이 .so 다 — 과업지시서 4장이 요구하는 형태다.

    ros2 launch video_streamer video_streamer.launch.py
    ros2 launch video_streamer video_streamer.launch.py encoder:=x264enc   # 개발 PC
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import ComposableNodeContainer
from launch_ros.descriptions import ComposableNode
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    config = PathJoinSubstitution(
        [FindPackageShare("video_streamer"), "config", "video_streamer.yaml"])

    container = ComposableNodeContainer(
        name="camera_container",
        namespace="",
        package="rclcpp_components",
        # intra-process 를 쓰려면 멀티스레드 실행기여야 한다. 단일 스레드로
        # 두면 인코딩이 카메라 콜백을 막는다.
        executable="component_container_mt",
        composable_node_descriptions=[
            ComposableNode(
                package="realsense2_camera",
                plugin="realsense2_camera::RealSenseNodeFactory",
                name="arm_camera",
                namespace="fr3/camera",
                parameters=[{
                    "serial_no": LaunchConfiguration("serial"),
                    "enable_color": True,
                    "enable_depth": True,
                    "pointcloud.enable": True,
                }],
                extra_arguments=[{"use_intra_process_comms": True}],
            ),
            ComposableNode(
                package="video_streamer",
                plugin="video_streamer::VideoStreamerNode",
                name="video_streamer",
                namespace="fr3/camera",
                parameters=[config, {
                    "encoder": LaunchConfiguration("encoder"),
                    "bind_address": LaunchConfiguration("bind_address"),
                }],
                extra_arguments=[{"use_intra_process_comms": True}],
            ),
        ],
        output="screen",
    )

    return LaunchDescription([
        DeclareLaunchArgument("encoder", default_value="nvv4l2h264enc",
                              description="젯슨은 nvv4l2h264enc, 개발 PC 는 x264enc"),
        DeclareLaunchArgument("bind_address", default_value="127.0.0.1",
                              description="RTSP 서버가 들을 주소. 내부망만."),
        DeclareLaunchArgument("serial", default_value=""),
        container,
    ])

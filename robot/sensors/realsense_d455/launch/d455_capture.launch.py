"""Start the arm-mounted D455 for still-image capture.

The driver continuously supplies frames inside the robot so a capture request
can use the current image. The HMI receives no live video; it receives one
preview only after a capture request succeeds.
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import ComposableNodeContainer
from launch_ros.descriptions import ComposableNode
from launch_ros.substitutions import FindPackageShare


NAMESPACE = "fr3/camera"
REMAPPINGS = [
    ("~/color/image_raw", "/fr3/camera_2d/image_raw"),
    ("~/color/camera_info", "/fr3/camera_2d/camera_info"),
    ("~/depth/color/points", "/fr3/camera_3d/points"),
    ("~/aligned_depth_to_color/image_raw", "/fr3/camera_3d/image_raw"),
]


def _container(context, *_args, **_kwargs):
    serial = LaunchConfiguration("serial").perform(context)
    frame_prefix = LaunchConfiguration("frame_prefix").perform(context)
    camera_package = FindPackageShare("realsense_d455")
    overrides = {
        "camera_name": "d455",
        "frame_id": f"{frame_prefix}d455_camera_link",
    }
    if serial:
        overrides["serial_no"] = serial

    return [ComposableNodeContainer(
        name="d455_capture_container",
        namespace="",
        package="rclcpp_components",
        executable="component_container_mt",
        composable_node_descriptions=[
            ComposableNode(
                package="realsense2_camera",
                plugin="realsense2_camera::RealSenseNodeFactory",
                name="d455_camera",
                namespace=NAMESPACE,
                parameters=[
                    PathJoinSubstitution([camera_package, "config", "d455.yaml"]),
                    overrides,
                ],
                remappings=REMAPPINGS,
                extra_arguments=[{"use_intra_process_comms": True}],
            ),
        ],
        output="screen",
    )]


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument(
            "serial", default_value="",
            description="D455 serial number; set it when more than one camera is connected."),
        DeclareLaunchArgument(
            "frame_prefix", default_value="",
            description="Prefix for D455 TF frame names."),
        OpaqueFunction(function=_container),
    ])

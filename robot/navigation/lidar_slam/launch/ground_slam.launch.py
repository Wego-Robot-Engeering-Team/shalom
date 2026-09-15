"""3D LiDAR ground segmentation -> ground-relative obstacle filter -> 2D SLAM.

Takes a PointCloud2 and a robot frame; everything above that is the caller's job.
Nothing here knows about a particular robot or simulator, so the same chain runs
against Gazebo, MuJoCo or hardware -- point `pointcloud_topic` at whatever
publishes the scan.

    ros2 launch lidar_slam ground_slam.launch.py \
        pointcloud_topic:=/b2/points base_frame:=base_link

Chain:
    <cloud> -> ground_segmentation  -> ground_points / obstacle_points
                                    -> ground_filter -> slam_points
                                    -> pointcloud_to_laserscan -> /obstacle_scan
                                    -> slam_toolbox -> /map, map -> odom

`odom -> base_frame` is NOT provided here.  Supply it from wheel/leg odometry or
a LiDAR odometry node (KISS-ICP); two publishers on that edge corrupt the tree.
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    ground_seg = FindPackageShare("ground_segmentation_ros2")
    slam = FindPackageShare("slam_toolbox")

    use_sim_time = LaunchConfiguration("use_sim_time")
    base_frame = LaunchConfiguration("base_frame")

    segmentation = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([ground_seg, "/launch/ground_segmentation.launch.py"]),
        launch_arguments={
            "pointcloud_topic": LaunchConfiguration("pointcloud_topic"),
            "params_file": LaunchConfiguration("gseg_params_file"),
            "use_sim_time": use_sim_time,
        }.items(),
    )

    ground_filter = Node(
        package="lidar_slam",
        executable="ground_filter",
        name="ground_filter",
        remappings=[
            ("ground", "/ground_segmentation/ground_points"),
            ("obstacles", "/ground_segmentation/obstacle_points"),
            ("filtered", "/ground_segmentation/slam_points"),
        ],
        parameters=[
            LaunchConfiguration("ground_filter_params_file"),
            {"use_sim_time": use_sim_time},
        ],
        output="screen",
    )

    obstacle_scan = Node(
        package="pointcloud_to_laserscan",
        executable="pointcloud_to_laserscan_node",
        name="obstacle_scan",
        remappings=[
            ("cloud_in", "/ground_segmentation/slam_points"),
            ("scan", "/obstacle_scan"),
        ],
        parameters=[{
            "use_sim_time": use_sim_time,
            "target_frame": base_frame,
            "transform_tolerance": 0.2,
            # Height was already filtered against the local ground; a second
            # fixed cutoff here would undo that on slopes.
            "min_height": -5.0,
            "max_height": 5.0,
            "angle_min": -3.14159265,
            "angle_max": 3.14159265,
            "angle_increment": 0.01,
            "scan_time": 0.1,
            "range_min": LaunchConfiguration("range_min"),
            "range_max": LaunchConfiguration("range_max"),
            "use_inf": True,
            "inf_epsilon": 1.0,
        }],
        output="screen",
    )

    mapper = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([slam, "/launch/online_async_launch.py"]),
        launch_arguments={
            "use_sim_time": use_sim_time,
            "autostart": "true",
            "slam_params_file": LaunchConfiguration("slam_params_file"),
        }.items(),
        condition=IfCondition(LaunchConfiguration("slam")),
    )

    return LaunchDescription([
        DeclareLaunchArgument("pointcloud_topic", default_value="/points",
                              description="Input 3D LiDAR PointCloud2."),
        DeclareLaunchArgument("base_frame", default_value="base_link",
                              description="Robot frame the 2D scan is expressed in."),
        DeclareLaunchArgument("use_sim_time", default_value="true"),
        DeclareLaunchArgument("slam", default_value="true",
                              description="Run slam_toolbox. Off when localizing against a saved map."),
        DeclareLaunchArgument("range_min", default_value="0.4"),
        DeclareLaunchArgument("range_max", default_value="30.0"),
        # 설정 파일은 부르는 쪽이 준다. 기본값을 두지 않는다.
        #
        # 예전에는 이 패키지 안에 한 벌을 두고 기본값으로 걸어 두었는데, 실제로
        # 도는 것은 늘 robot_bringup 이 넘기는 navigation/config/ 쪽이었다.
        # 두 벌이 갈라져도 아무도 몰랐고 — 지면 높이가 1.20 과 1.50 으로 달랐다 —
        # 패키지 안의 것을 고치면 아무 일도 일어나지 않았다. slam.yaml 은
        # 아예 없는 파일을 가리키고 있었다.
        #
        # 이 값들은 로봇마다 다르다. 라이다 장착 높이와 로봇 좌표계가 들어
        # 있으므로, "그럴듯한 기본값" 이라는 것이 존재하지 않는다.
        DeclareLaunchArgument(
            "gseg_params_file",
            description="지면 분리 파라미터. 라이다 장착 높이와 로봇 좌표계가 "
                        "여기 들어 있다.",
        ),
        DeclareLaunchArgument(
            "ground_filter_params_file",
            description="지면 기준 장애물 필터 파라미터.",
        ),
        DeclareLaunchArgument(
            "slam_params_file",
            description="slam_toolbox 파라미터.",
        ),
        segmentation, ground_filter, obstacle_scan, mapper,
    ])

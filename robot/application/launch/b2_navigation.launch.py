"""Autonomy bring-up for the Unitree B2.

Ties four layers together:

    robot        b2_simulation (MuJoCo stand-in) or b2_driver (hardware)
    perception   nav2_3d  -- ground segmentation, ground-relative filter, 2D SLAM
    planning     Nav2
    station      shalom_bridge -- the TCP boundary the control station connects to

The robot layer is a launch argument because both options expose the same
interface: a PointCloud2 out, `/cmd_vel` in.  Nothing below this file knows which
one is running.

    ros2 launch application b2_navigation.launch.py robot:=sim
    ros2 launch application b2_navigation.launch.py robot:=real

`payload:=fr3` runs the B2 that carries a FAIRINO FR3 arm, with the policy
trained for its 105 kg and higher centre of mass.  Simulator only -- on hardware
the payload is whatever is actually bolted to the robot.

TWO WAYS TO HAVE A MAP
----------------------
    (default)        slam_toolbox builds one as the robot drives
    map:=<file.yaml> map_server serves a saved one and AMCL localises in it

    ros2 launch application b2_navigation.launch.py \
        map:=$(ros2 pkg prefix application)/share/application/maps/2026-09-07.yaml

Which of the two is running is the *robot's* state, not a display option: the
control station draws the map the robot sends on `map/occupancy`, so it always
shows what the robot is actually navigating against. Letting the station load a
different map for display would put two maps on one screen with nothing saying
which one the planner believes.

Mapping and localisation are mutually exclusive here because both would own
`map -> odom` and two publishers on one transform edge corrupt the tree - the
same reason ground-truth odometry is turned off when KISS-ICP is running.

The bridge belongs to the robot, not to the station: the station runs no ROS 2
at all and reaches this machine over a single TCP port. So it comes up with
everything else rather than being started by hand, and `bridge:=false` leaves it
out when you only want the robot.

`odom -> base_link` comes from KISS-ICP.  The simulator can publish ground-truth
odometry instead, but not at the same time: two publishers on one transform edge
corrupt the tree, so `b2_sim.launch.py` is started here with that turned off.
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.conditions import IfCondition, LaunchConfigurationEquals
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import (
    LaunchConfiguration,
    PathJoinSubstitution,
    PythonExpression,
)
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare

POINTS_TOPIC = "/b2/points"
BASE_FRAME = "base_link"


def generate_launch_description():
    bringup = FindPackageShare("application")
    ground_seg = FindPackageShare("slam_3d_to_2d")
    nav2_bringup = FindPackageShare("nav2_bringup")
    kiss_icp = FindPackageShare("kiss_icp")
    shalom_bridge = FindPackageShare("shalom_bridge")

    # A map file was given, so serve it and localise instead of mapping.
    localising = PythonExpression(["'", LaunchConfiguration("map"), "' != ''"])

    use_sim_time = LaunchConfiguration("use_sim_time")

    # --- robot layer: simulator or hardware ---------------------------------
    sim_robot = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([
            FindPackageShare("b2_mujoco"), "/launch/b2_sim.launch.py",
        ]),
        launch_arguments={
            "viewer": LaunchConfiguration("viewer"),
            "payload": LaunchConfiguration("payload"),
            # KISS-ICP owns odom -> base_link in this stack.
            "ground_truth_tf": "false",
        }.items(),
        condition=LaunchConfigurationEquals("robot", "sim"),
    )

    real_robot = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([
            FindPackageShare("b2_base"), "/launch/b2_bringup.launch.py",
        ]),
        launch_arguments={
            "network_interface": LaunchConfiguration("network_interface"),
        }.items(),
        condition=LaunchConfigurationEquals("robot", "real"),
    )

    # --- perception ----------------------------------------------------------
    perception = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([ground_seg, "/launch/ground_slam.launch.py"]),
        launch_arguments={
            "pointcloud_topic": LaunchConfiguration("pointcloud_topic"),
            "base_frame": BASE_FRAME,
            "use_sim_time": use_sim_time,
            # slam_toolbox stands down when a saved map is being served.
            "slam": PythonExpression(
                ["'false' if '", LaunchConfiguration("map"), "' else '",
                 LaunchConfiguration("slam"), "'"]),
            "gseg_params_file": PathJoinSubstitution([bringup, "config", "gseg3d_b2.yaml"]),
            "ground_filter_params_file": PathJoinSubstitution(
                [bringup, "config", "ground_filter_b2.yaml"]),
            "slam_params_file": PathJoinSubstitution([bringup, "config", "slam_b2.yaml"]),
        }.items(),
    )

    odometry = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([kiss_icp, "/launch/odometry.launch.py"]),
        launch_arguments={
            "topic": LaunchConfiguration("pointcloud_topic"),
            "base_frame": BASE_FRAME,
            "lidar_odom_frame": "odom",
            "invert_odom_tf": "False",
            "visualize": "False",
            "config_file": PathJoinSubstitution([bringup, "config", "kiss_icp_b2.yaml"]),
            "use_sim_time": use_sim_time,
        }.items(),
    )

    # --- planning ------------------------------------------------------------
    nav2 = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([nav2_bringup, "launch", "bringup_launch.py"])),
        launch_arguments={
            "use_sim_time": use_sim_time,
            "slam": "False",              # slam_toolbox is launched by nav2_3d
            "use_localization": "False",
            "autostart": "True",
            "use_composition": "False",
            "use_respawn": "False",
            "params_file": PathJoinSubstitution([bringup, "config", "nav2_b2.yaml"]),
        }.items(),
        condition=IfCondition(LaunchConfiguration("nav2")),
    )

    # --- saved map -----------------------------------------------------------
    #
    # Started as plain nodes rather than through nav2_bringup's map/localisation
    # launch because that file also brings up its own lifecycle manager, and a
    # second manager racing the navigation one leaves both waiting on bonds that
    # never form.
    map_server = Node(
        package="nav2_map_server",
        executable="map_server",
        name="map_server",
        output="screen",
        parameters=[{"use_sim_time": use_sim_time,
                     "yaml_filename": LaunchConfiguration("map")}],
        condition=IfCondition(localising),
    )

    amcl = Node(
        package="nav2_amcl",
        executable="amcl",
        name="amcl",
        output="screen",
        parameters=[PathJoinSubstitution([bringup, "config", "amcl_b2.yaml"]),
                    {"use_sim_time": use_sim_time}],
        condition=IfCondition(localising),
    )

    # These two need their own lifecycle manager: nav2_bringup's is configured
    # with `slam:=False, use_localization:=False` above, so it manages neither.
    localisation_manager = Node(
        package="nav2_lifecycle_manager",
        executable="lifecycle_manager",
        name="lifecycle_manager_localization",
        output="screen",
        parameters=[{"use_sim_time": use_sim_time,
                     "autostart": True,
                     "node_names": ["map_server", "amcl"]}],
        condition=IfCondition(localising),
    )

    # --- station link --------------------------------------------------------
    #
    # Started through the bridge's own launch file rather than as a Node here,
    # because that file carries the respawn policy: if the bridge dies the
    # safety node stops the robot on the missing heartbeat, and the bridge has
    # to come back on its own so the operator can reconnect.
    bridge = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([shalom_bridge, "launch", "bridge.launch.py"])),
        launch_arguments={"use_sim_time": use_sim_time}.items(),
        condition=IfCondition(LaunchConfiguration("bridge")),
    )

    rviz = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([nav2_bringup, "launch", "rviz_launch.py"])),
        launch_arguments={
            "use_sim_time": use_sim_time,
            "rviz_config": PathJoinSubstitution([bringup, "rviz", "b2_slam.rviz"]),
        }.items(),
        condition=IfCondition(LaunchConfiguration("rviz")),
    )

    return LaunchDescription([
        DeclareLaunchArgument("robot", default_value="sim",
                              choices=["sim", "real"],
                              description="MuJoCo stand-in or the physical B2."),
        DeclareLaunchArgument("use_sim_time", default_value="true",
                              description="Set false when robot:=real."),
        DeclareLaunchArgument("pointcloud_topic", default_value=POINTS_TOPIC),
        DeclareLaunchArgument("slam", default_value="true",
                              description="Build a map while driving. Ignored when "
                                          "`map` names a saved one."),
        DeclareLaunchArgument("map", default_value="",
                              description="Path to a saved map's .yaml. Given, the robot "
                                          "localises in it instead of mapping."),
        DeclareLaunchArgument("nav2", default_value="true"),
        DeclareLaunchArgument("rviz", default_value="true"),
        DeclareLaunchArgument("bridge", default_value="true",
                              description="Accept the control station on TCP 9090."),
        DeclareLaunchArgument("viewer", default_value="true",
                              description="MuJoCo viewer window (robot:=sim only)."),
        DeclareLaunchArgument("payload", default_value="none",
                              choices=["none", "fr3"],
                              description="Carry a FAIRINO FR3 arm (robot:=sim only). "
                                          "Selects the matching scene and policy."),
        DeclareLaunchArgument("network_interface", default_value="",
                              description="Ethernet interface to the robot (robot:=real only)."),
        sim_robot, real_robot, perception, odometry,
        map_server, amcl, localisation_manager,
        nav2, bridge, rviz,
    ])

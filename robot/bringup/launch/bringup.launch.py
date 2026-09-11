"""Brings the whole inspection system up.

This is the one command that starts everything. Navigation is one layer of it,
not the point of it - the file was called b2_navigation for a while and that
name undersold what it does.

    robot        b2_simulation (MuJoCo stand-in) or b2_driver (hardware)
    perception   ground segmentation, ground-relative filter, 2D SLAM
    localisation saved map + AMCL, or live SLAM
    planning     Nav2
    camera       RealSense; the bridge saves originals on request
    station      hmi_bridge -- the TCP boundary the control station connects to

The robot layer is a launch argument because both options expose the same
interface: a PointCloud2 out, `/cmd_vel` in.  Nothing below this file knows which
one is running.

    ros2 launch bringup bringup.launch.py robot:=sim
    ros2 launch bringup bringup.launch.py robot:=real
    ros2 launch bringup bringup.launch.py robot:=none    # 로봇 없이 센서만

`robot:=none` 은 실기가 아직 없는 자리에서 센서·브릿지·관제 연동을 시험하기
위한 것이다. 로봇 계층 대신 정지 오도메트리를 올려 `odom -> base_link` 를
채운다 — 그 한 변이 비면 TF 트리가 끊겨 브릿지가 자세를 못 읽고, `state/pose`
가 멎으며 촬영은 "이동 중" 으로 거절된다. 로봇이 없다는 사실이 촬영이 안 되는
이유로 나타나므로, 그 자리에서 무엇을 시험하든 먼저 이것에 걸린다.

`payload:=fr3` runs the B2 that carries a FAIRINO FR3 arm, with the policy
trained for its 105 kg and higher centre of mass.  Simulator only -- on hardware
the payload is whatever is actually bolted to the robot.

TWO WAYS TO HAVE A MAP
----------------------
    (default)        slam_toolbox builds one as the robot drives
    map:=<file.yaml> map_server serves a saved one and AMCL localises in it

    ros2 launch bringup bringup.launch.py \
        map:=$(ros2 pkg prefix bringup)/share/bringup/navigation/maps/2026-09-07.yaml

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
import os
from pathlib import Path

from ament_index_python.packages import get_package_share_directory
from launch.actions import (DeclareLaunchArgument, IncludeLaunchDescription,
                            OpaqueFunction, SetEnvironmentVariable,
                            SetLaunchConfiguration)
from launch.conditions import (IfCondition, LaunchConfigurationEquals,
                               UnlessCondition)
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


def _resolve_map(context, *_a, **_k):
    """`map` 인자를 실제 파일 경로로 바꾼다.

    지금까지는 전체 경로를 그대로 쳐야 했다. 검수고에서 날마다 지도를 새로
    뜨는데 매번 40 자를 치는 것은 실수하기 좋은 일이라, 이름만으로도,
    `latest` 로도 받는다.

        map:=latest        maps/ 에서 가장 최근 것
        map:=2026-09-07    navigation/maps/2026-09-07.yaml
        map:=/절대/경로.yaml
        map:=none          저장된 지도를 쓰지 않고 실시간 SLAM
    """
    raw = LaunchConfiguration("map").perform(context).strip()
    if raw in ("", "none", "slam"):
        return [SetLaunchConfiguration("map", "")]

    maps_dir = Path(get_package_share_directory("bringup")) / "navigation" / "maps"

    if raw == "latest":
        found = sorted(maps_dir.glob("*.yaml"))
        if not found:
            raise RuntimeError(
                f"{maps_dir} 에 지도가 없다. 실시간 SLAM 으로 돌리려면 map:=none.")
        resolved = found[-1]
    elif os.path.isabs(raw):
        resolved = Path(raw)
    else:
        resolved = maps_dir / (raw if raw.endswith(".yaml") else raw + ".yaml")

    if not resolved.is_file():
        have = ", ".join(p.stem for p in sorted(maps_dir.glob("*.yaml"))) or "(없음)"
        raise RuntimeError(f"그런 지도가 없다: {resolved}\n  있는 것: {have}")

    return [SetLaunchConfiguration("map", str(resolved))]


def generate_launch_description():
    bringup = FindPackageShare("bringup")
    navigation_config = PathJoinSubstitution([bringup, "navigation", "config"])
    navigation_rviz = PathJoinSubstitution([bringup, "navigation", "rviz"])
    lidar_slam = FindPackageShare("lidar_slam")
    realsense_d455 = FindPackageShare("realsense_d455")
    velodyne_vlp16 = FindPackageShare("velodyne_vlp16")
    aurora = FindPackageShare("aurora")
    nav2_bringup = FindPackageShare("nav2_bringup")
    kiss_icp = FindPackageShare("kiss_icp")
    hmi_bridge = FindPackageShare("hmi_bridge")

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

    # 로봇이 없는 자리를 위한 정지 오도메트리. 값을 지어내지 않고 항등 변환을
    # 낼 뿐이라, 화면에 위치가 늘 원점으로 보인다 — 실주행으로 오해할 여지가
    # 없고, 없으면 시험 자체가 불가능한 한 변만 채운다.
    bench_robot = Node(
        package="bringup",
        executable="bench_odom",
        name="bench_odom",
        output="screen",
        parameters=[{"use_sim_time": use_sim_time, "base_frame": BASE_FRAME}],
        condition=LaunchConfigurationEquals("robot", "none"),
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
        PythonLaunchDescriptionSource([lidar_slam, "/launch/ground_slam.launch.py"]),
        launch_arguments={
            "pointcloud_topic": LaunchConfiguration("pointcloud_topic"),
            "base_frame": BASE_FRAME,
            "use_sim_time": use_sim_time,
            # slam_toolbox stands down when a saved map is being served.
            "slam": PythonExpression(
                ["'false' if '", LaunchConfiguration("map"), "' else '",
                 LaunchConfiguration("slam"), "'"]),
            "gseg_params_file": PathJoinSubstitution(
                [navigation_config, "ground_segmentation.yaml"]),
            "ground_filter_params_file": PathJoinSubstitution(
                [navigation_config, "ground_filter.yaml"]),
            "slam_params_file": PathJoinSubstitution(
                [navigation_config, "slam_toolbox.yaml"]),
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
            "config_file": PathJoinSubstitution([navigation_config, "kiss_icp.yaml"]),
            "use_sim_time": use_sim_time,
        }.items(),
        # bench_odom 이 odom -> base_link 를 소유한다. 두 publisher 가 한 변을
        # 가지면 트리가 망가지므로 둘 중 하나만 돈다.
        condition=UnlessCondition(
            PythonExpression(["'", LaunchConfiguration("robot"), "' == 'none'"])),
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
            "params_file": PathJoinSubstitution([navigation_config, "nav2.yaml"]),
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
        parameters=[PathJoinSubstitution([navigation_config, "amcl.yaml"]),
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
            PathJoinSubstitution([hmi_bridge, "launch", "bridge.launch.py"])),
        launch_arguments={"use_sim_time": use_sim_time,
                          "robot_id": LaunchConfiguration("robot_id"),
                          "robot_name": LaunchConfiguration("robot_name")}.items(),
        condition=IfCondition(LaunchConfiguration("bridge")),
    )

    # 로봇암 D455. 프레임은 hmi_bridge가 직접 구독해 촬영 때 원본으로
    # 저장한다 — 실시간 송출 경로는 없다.
    cameras = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution(
                [realsense_d455, "launch", "d455_stream.launch.py"])),
        launch_arguments={
            "role": "arm",
            "serial": LaunchConfiguration("arm_camera_serial"),
        }.items(),
        condition=IfCondition(LaunchConfiguration("cameras")),
    )

    # Aurora는 초기에는 원시 odom만 별도 프레임으로 올린다. KISS-ICP의
    # odom -> base_link와 충돌시키지 않고 장착 외부파라미터를 검증하기 위한
    # 단계다. 주 odom 전환은 calibration 뒤에 별도로 한다.
    aurora = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([aurora, "launch", "aurora_s.launch.py"])),
        launch_arguments={"ip_address": LaunchConfiguration("aurora_ip")}.items(),
        condition=IfCondition(LaunchConfiguration("aurora")),
    )

    # 임시 시험용 VLP-16. B2 내장 라이다가 없는 자리에서 인식·SLAM 을
    # 돌려보기 위한 것이라, 토픽과 좌표계를 B2 것에 맞춰 끼운다.
    vlp16 = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([velodyne_vlp16, "launch", "vlp16.launch.py"])),
        launch_arguments={"points_topic": LaunchConfiguration("pointcloud_topic")}.items(),
        condition=LaunchConfigurationEquals("lidar", "vlp16"),
    )

    rviz = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([nav2_bringup, "launch", "rviz_launch.py"])),
        launch_arguments={
            "use_sim_time": use_sim_time,
            "rviz_config": PathJoinSubstitution([navigation_rviz, "b2_slam.rviz"]),
        }.items(),
        condition=IfCondition(LaunchConfiguration("rviz")),
    )

    return LaunchDescription([
        # DDS 도메인. 로봇 한 대일 때는 기본값 그대로 두면 된다.
        #
        # 여러 대를 같은 망에 올릴 때 반드시 갈라야 하는 값이다. 토픽 이름이
        # 절대경로로 고정돼 있어서(/b2/points, /tf, /cmd_vel ...) 같은
        # 도메인에 둘을 올리면 서로의 라이다와 TF 를 구독한다. /tf 가 특히
        # 치명적이다 — 두 로봇의 map->odom->base_link 가 한 트리에 섞이면
        # 위치추정이 통째로 깨지고, 1 호기에 보낸 속도 명령을 2 호기가 받는다.
        #
        # 네임스페이스로 이름만 가르는 방법도 있지만 DDS 디스커버리는 여전히
        # 공유한다. 로봇이 늘수록 무선 대역을 서로 갉아먹으므로, 검수고
        # 환경에서는 도메인을 나누는 편이 맞다.
        DeclareLaunchArgument("domain_id", default_value="0",
                              description="ROS_DOMAIN_ID. 로봇마다 다르게 준다."),
        SetEnvironmentVariable("ROS_DOMAIN_ID", LaunchConfiguration("domain_id")),

        # 어느 DDS 구현을 쓸지 못박는다.
        #
        # ROS 2 Jazzy 의 기본은 Fast DDS 다. 그대로 두면 아래 CYCLONEDDS_URI 가
        # 통째로 무시되고, 노드들이 0.0.0.0:7400 으로 모든 인터페이스에 열린다 —
        # 같은 망의 다른 기기가 도메인만 맞으면 그대로 붙는다. 과업지시서 7.1 이
        # 막으라는 통신이 바로 그것이다.
        #
        # 증상이 없다는 점이 나쁘다. 한 대만 돌리는 동안에는 모든 것이 정상으로
        # 보이고, 격리가 걸렸는지는 소켓을 들여다봐야만 알 수 있다.
        SetEnvironmentVariable("RMW_IMPLEMENTATION", "rmw_cyclonedds_cpp"),

        # DDS 설정을 실제로 물린다.
        #
        # config/cyclonedds.xml 은 진작 있었는데 아무도 읽지 않고 있었다.
        # 참가자 한도를 60 으로 올린 것도, DDS 를 루프백에 가두는 것도 이
        # 줄이 없으면 적용되지 않는다 — 파일만 두고 적용을 잊으면 증상이
        # 없으므로 그대로 넘어간다.
        # 스킴을 경로와 따로 이어 붙인다. PathJoinSubstitution 은 경로를
        # 정규화하면서 "file://" 를 먹어 버려, 값이 맨 경로로 나간다. Cyclone 은
        # 그것을 설정 파일로 읽지 않고 조용히 기본값으로 돈다 — 격리가 걸린 줄
        # 알았는데 노드가 바깥에서 보이던 것이 이 때문이었다.
        SetEnvironmentVariable(
            "CYCLONEDDS_URI",
            ["file://", PathJoinSubstitution([bringup, "config", "cyclonedds.xml"])]),

        DeclareLaunchArgument("robot_id", default_value="R1",
                              description="로봇 식별자. 관제가 어느 로봇인지 안다."),
        DeclareLaunchArgument("robot_name", default_value="1호기",
                              description="화면에 보일 이름"),

        DeclareLaunchArgument("robot", default_value="sim",
                              choices=["sim", "real", "none"],
                              description="MuJoCo stand-in, the physical B2, or "
                                          "none = 로봇 없이 센서만 (정지 오도메트리)."),
        DeclareLaunchArgument("use_sim_time", default_value="true",
                              description="Set false when robot:=real."),
        DeclareLaunchArgument("pointcloud_topic", default_value=POINTS_TOPIC),
        DeclareLaunchArgument("slam", default_value="true",
                              description="Build a map while driving. Ignored when "
                                          "`map` names a saved one."),
        DeclareLaunchArgument("map", default_value="latest",
                              description="latest | <이름> | <경로.yaml> | none. "
                                          "none 이면 실시간 SLAM."),
        DeclareLaunchArgument("nav2", default_value="true"),
        DeclareLaunchArgument("rviz", default_value="true"),
        DeclareLaunchArgument("lidar", default_value="none",
                              choices=["none", "vlp16"],
                              description="none 이면 로봇(시뮬·실기)이 점군을 낸다. "
                                          "vlp16 은 임시 시험용 외장 라이다."),
        DeclareLaunchArgument("cameras", default_value="true",
                              description="로봇암 RealSense. 촬영 원본의 출처다."),
        DeclareLaunchArgument("arm_camera_serial", default_value="",
                              description="두 대 이상 달았으면 반드시 지정"),
        DeclareLaunchArgument("aurora", default_value="false",
                              description="Aurora S 원시 odom을 함께 올릴지"),
        DeclareLaunchArgument("aurora_ip", default_value="192.168.11.1",
                              description="Aurora S SDK 주소"),
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
        # 인자가 모두 선언된 뒤, 노드가 뜨기 전에 map 을 실제 경로로 바꾼다.
        OpaqueFunction(function=_resolve_map),

        sim_robot, real_robot, bench_robot, perception, odometry,
        map_server, amcl, localisation_manager,
        nav2, bridge, cameras, aurora, vlp16, rviz,
    ])

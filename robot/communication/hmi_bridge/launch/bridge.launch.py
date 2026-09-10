"""브릿지 노드 기동.

안전 노드는 여기서 함께 띄우지 않는다. 브릿지가 죽어도 안전 노드는 살아
있어야 하고, 같은 launch 로 묶어 두면 한쪽 실패가 다른 쪽을 끌고 내려갈
여지가 생긴다. 두 프로세스는 각각 감시·재기동되어야 한다.
"""

from pathlib import Path

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
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

    # 시뮬레이터와 함께 돌 때는 /clock 을 따라야 한다. 브릿지가 벽시계를
    # 쓰면 TF 조회가 시뮬 시각과 어긋나 위치가 나가지 않는다. 실기에서는
    # false 로 둔다 — true 로 두면 오지 않는 /clock 을 기다리며 멈춘다.
    sim_time_arg = DeclareLaunchArgument(
        "use_sim_time",
        default_value="false",
        description="시뮬레이터와 함께 돌 때만 true",
    )

    # 로봇 식별자. 지금은 한 대뿐이라 화면에 이름을 띄우는 데만 쓰지만,
    # 여러 대가 되면 관제가 값을 가르는 근거가 된다.
    robot_id_arg = DeclareLaunchArgument(
        "robot_id", default_value="R1",
        description="로봇 식별자. 여러 대가 되면 관제가 이것으로 구분한다.")
    robot_name_arg = DeclareLaunchArgument(
        "robot_name", default_value="1호기",
        description="화면에 보일 이름")

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
        executable="bridge_node",
        name="hmi_bridge",
        output="screen",
        remappings=remaps,
        parameters=[LaunchConfiguration("config"),
                    {"use_sim_time": LaunchConfiguration("use_sim_time"),
                     "robot_id": LaunchConfiguration("robot_id"),
                     "robot_name": LaunchConfiguration("robot_name")}],
        # 브릿지가 죽으면 생존 신호가 끊기고 안전 노드가 로봇을 정지시킨다.
        # 그 뒤 자동으로 다시 올라와 관제가 재연결할 수 있게 한다.
        respawn=True,
        respawn_delay=2.0,
    )

    return LaunchDescription(
        [config_arg, sim_time_arg, robot_id_arg, robot_name_arg, bridge])

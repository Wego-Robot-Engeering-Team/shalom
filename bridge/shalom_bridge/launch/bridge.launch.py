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
        Path(get_package_share_directory("shalom_bridge")) / "config" / "bridge.yaml"
    )

    config_arg = DeclareLaunchArgument(
        "config",
        default_value=default_config,
        description="브릿지 파라미터 파일 경로",
    )

    bridge = Node(
        package="shalom_bridge",
        executable="bridge_node",
        name="shalom_bridge",
        output="screen",
        parameters=[LaunchConfiguration("config")],
        # 브릿지가 죽으면 생존 신호가 끊기고 안전 노드가 로봇을 정지시킨다.
        # 그 뒤 자동으로 다시 올라와 관제가 재연결할 수 있게 한다.
        respawn=True,
        respawn_delay=2.0,
    )

    return LaunchDescription([config_arg, bridge])

"""Ground-relative obstacle-only 2D SLAM for the demo.

The demo supplies KISS-ICP odometry plus ground and obstacle point clouds.
A local-ground filter removes points above the configured height band before
the remaining obstacles are projected into a 2D scan for slam_toolbox.
"""

import os
import platform

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import GroupAction, IncludeLaunchDescription, SetEnvironmentVariable
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.actions import Node


def generate_launch_description():
    tools_share = get_package_share_directory('robot_sim_tools')
    demo_share = get_package_share_directory('nav2_lidar_ground_segmentation_demo')
    slam_share = get_package_share_directory('slam_toolbox')

    demo = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            f'{demo_share}/launch/lidar_ground_segmentation_demo.launch.py'
        ),
        launch_arguments={'enable_slam': 'True'}.items(),
    )

    ground_filter = Node(
        package='robot_sim_tools',
        executable='ground_filter',
        name='ground_filter',
        remappings=[
            ('ground', '/ground_segmentation/ground_points'),
            ('obstacles', '/ground_segmentation/obstacle_points'),
            ('filtered', '/ground_segmentation/slam_points'),
        ],
        parameters=[f'{tools_share}/config/ground_filter.yaml'],
        output='screen',
    )

    # RoboStack on macOS needs Conda's Python symbols in C++ processes that
    # load ROS Python type-support libraries. Scope this only to the new
    # filter, keeping Gazebo and RViz isolated from DYLD injection.
    ground_filter_actions = [ground_filter]
    if platform.system() == 'Darwin':
        conda_prefix = os.environ.get('CONDA_PREFIX')
        if conda_prefix:
            ground_filter_actions = [
                GroupAction(actions=[
                    SetEnvironmentVariable(
                        'DYLD_INSERT_LIBRARIES',
                        os.path.join(conda_prefix, 'lib', 'libpython3.12.dylib'),
                    ),
                    ground_filter,
                ])
            ]

    obstacle_scan = Node(
        package='pointcloud_to_laserscan',
        executable='pointcloud_to_laserscan_node',
        name='obstacle_scan',
        remappings=[
            ('cloud_in', '/ground_segmentation/slam_points'),
            ('scan', '/obstacle_scan'),
        ],
        parameters=[{
            'use_sim_time': True,
            'target_frame': 'husky/base_link',
            'transform_tolerance': 0.2,
            # Height is filtered relative to the local ground beforehand.
            # Do not reapply a fixed, world-height-like cutoff here.
            'min_height': -5.0,
            'max_height': 5.0,
            'angle_min': -3.14159265,
            'angle_max': 3.14159265,
            'angle_increment': 0.01,
            'scan_time': 0.1,
            'range_min': 0.25,
            'range_max': 30.0,
            'use_inf': True,
            'inf_epsilon': 1.0,
        }],
        output='screen',
    )

    mapper = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(f'{slam_share}/launch/online_async_launch.py'),
        launch_arguments={
            'use_sim_time': 'True',
            'autostart': 'true',
            'slam_params_file': f'{tools_share}/config/slam.yaml',
        }.items(),
    )

    return LaunchDescription([demo, *ground_filter_actions, obstacle_scan, mapper])

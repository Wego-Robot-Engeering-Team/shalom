# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Exercise sensor launch options without opening serial/network devices."""

import importlib.util
from pathlib import Path

import pytest
from launch import LaunchContext
from launch.actions import DeclareLaunchArgument, GroupAction, IncludeLaunchDescription
from launch.utilities import normalize_to_list_of_substitutions, perform_substitutions
from launch_ros.actions import Node, SetRemap
from launch_ros.substitutions import FindPackageShare
from launch_ros.utilities import evaluate_parameters


ROOT = Path(__file__).resolve().parents[4]
SHARES = {name: ROOT / "robot/l1_drivers" / name for name in (
    "pandar_xt32", "vectornav_vn100", "velodyne_vlp16", "slamtec_aurora")}
SHARES["robot_bringup"] = ROOT / "robot/bringup/robot_bringup"


def resolve(context, value):
    return perform_substitutions(context, normalize_to_list_of_substitutions(value))


def load(package, filename, monkeypatch, **overrides):
    monkeypatch.setattr(FindPackageShare, "find",
                        lambda self, name: str(SHARES.get(name, Path("/test/share") / name)))
    spec = importlib.util.spec_from_file_location("sensor_test_launch", SHARES[package] / "launch" / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    launch = module.generate_launch_description()
    context = LaunchContext()
    context.launch_configurations.update(overrides)
    for action in launch.entities:
        if isinstance(action, DeclareLaunchArgument):
            action.execute(context)
    return launch, context


def selected(entities, context, kind):
    result = []
    for entity in entities:
        if entity.condition is not None and not entity.condition.evaluate(context):
            continue
        if isinstance(entity, kind):
            result.append(entity)
        elif isinstance(entity, GroupAction):
            result.extend(selected(entity.get_sub_entities(), context, kind))
    return result


@pytest.mark.parametrize("rviz,start_sensor,expected", [
    ("false", "true", ["vectornav_driver_node", "vn100_imu_adapter"]),
    ("true", "true", ["vectornav_driver_node", "vn100_imu_adapter", "vn100_attitude_visualizer",
                      "static_transform_publisher", "rviz2"]),
    ("true", "false", ["vn100_attitude_visualizer", "static_transform_publisher", "rviz2"]),
    ("false", "false", []),
])
def test_vn100_driver_adapter_and_visualization_options(monkeypatch, rviz, start_sensor, expected):
    launch, context = load("vectornav_vn100", "vn100.launch.py", monkeypatch,
                           rviz=rviz, start_sensor=start_sensor)
    assert [resolve(context, node.node_executable) for node in selected(launch.entities, context, Node)] == expected


def test_vn100_custom_serial_settings_and_visualizer_input(monkeypatch):
    launch, context = load("vectornav_vn100", "vn100.launch.py", monkeypatch, rviz="true",
                           config_file="/tmp/vn100.yaml", port="/dev/test_serial", imu_topic="/test/imu")
    driver, adapter, visualizer, tf, viewer = selected(launch.entities, context, Node)
    assert list(evaluate_parameters(context, driver._Node__parameters)) == [
        Path("/tmp/vn100.yaml"), {"port": "/dev/test_serial"}]
    assert list(evaluate_parameters(context, visualizer._Node__parameters)) == [{"imu_topic": "/test/imu"}]
    assert [resolve(context, value) for value in tf._Node__arguments][-4:] == [
        "--frame-id", "vn100_world", "--child-frame-id", "vn100_visualization"]
    assert [resolve(context, value) for value in viewer._Node__arguments] == [
        "-d", str(SHARES["vectornav_vn100"] / "rviz/vn100.rviz")]


@pytest.mark.parametrize("rviz,start_sensor,expected_nodes,expected_includes", [
    ("false", "true", ["tf2_ros", "tf2_ros"], 1),
    ("true", "true", ["tf2_ros", "tf2_ros", "rviz2"], 1),
    ("true", "false", ["rviz2"], 0),
    ("false", "false", [], 0),
])
def test_vlp16_driver_tf_and_viewer_options(monkeypatch, rviz, start_sensor, expected_nodes, expected_includes):
    launch, context = load("velodyne_vlp16", "vlp16.launch.py", monkeypatch,
                           rviz=rviz, start_sensor=start_sensor)
    assert [node.node_package for node in selected(launch.entities, context, Node)] == expected_nodes
    assert len(selected(launch.entities, context, IncludeLaunchDescription)) == expected_includes
    assert len(selected(launch.entities, context, SetRemap)) == expected_includes


def test_vlp16_viewer_keeps_custom_mount_and_topic(monkeypatch):
    launch, context = load("velodyne_vlp16", "vlp16.launch.py", monkeypatch, rviz="true",
                           points_topic="/test/points", fixed_frame="velodyne",
                           base_frame="vehicle", x="0.1", y="0.2", z="0.3", yaw="0.4")
    mount, alias, viewer = selected(launch.entities, context, Node)
    assert [resolve(context, value) for value in mount._Node__arguments] == [
        "--x", "0.1", "--y", "0.2", "--z", "0.3", "--yaw", "0.4",
        "--frame-id", "vehicle", "--child-frame-id", "velodyne"]
    assert tuple(resolve(context, value) for value in viewer._Node__remappings[0]) == (
        "/b2/points", "/test/points")


@pytest.mark.parametrize("odom,imaging,expected", [
    ("true", "false", ["slamware_ros_sdk_server_node"]),
    ("false", "true", ["aurora_imaging"]),
    ("true", "true", ["slamware_ros_sdk_server_node", "aurora_imaging"]),
    ("false", "false", []),
])
def test_aurora_independent_acquisition_modes(monkeypatch, odom, imaging, expected):
    launch, context = load("slamtec_aurora", "aurora_s.launch.py", monkeypatch, odom=odom, imaging=imaging)
    assert [resolve(context, node.node_executable) for node in selected(launch.entities, context, Node)] == expected


def test_aurora_single_config_and_shared_ip_override(monkeypatch):
    launch, context = load("slamtec_aurora", "aurora_s.launch.py", monkeypatch,
                           imaging="true", config_file="/tmp/aurora.yaml",
                           ip_address="192.0.2.10", frame_topic="/test/imaging")
    odom, imaging = selected(launch.entities, context, Node)
    assert list(evaluate_parameters(context, odom._Node__parameters)) == [
        Path("/tmp/aurora.yaml"), {"ip_address": "192.0.2.10"}]
    assert list(evaluate_parameters(context, imaging._Node__parameters)) == [
        Path("/tmp/aurora.yaml"), {"ip_address": "192.0.2.10", "frame_topic": "/test/imaging"}]


@pytest.mark.parametrize("package,filename,sensor_config,expected", [
    ("vectornav_vn100", "vn100.launch.py", "vn100_config_file",
     ["vectornav_driver_node", "vn100_imu_adapter"]),
    ("pandar_xt32", "xt32.launch.py", "xt32_config_file",
     ["hesai_ros_driver_node", "static_transform_publisher", "static_transform_publisher"]),
    ("slamtec_aurora", "aurora_s.launch.py", None, ["slamware_ros_sdk_server_node"]),
])
def test_whole_stack_sensor_options_are_scoped_and_do_not_open_extra_viewers(
    monkeypatch, package, filename, sensor_config, expected,
):
    launch, context = load("robot_bringup", "drivers.launch.py", monkeypatch, rviz="true",
                           vn100="true", aurora="true", odom="false", imaging="true")
    baseline = dict(context.launch_configurations)
    matched = []
    for group in launch.entities:
        if not isinstance(group, GroupAction):
            continue
        for action in group.execute(context):
            if isinstance(action, IncludeLaunchDescription):
                forwarded = {name: resolve(context, value) for name, value in action.launch_arguments}
                context.launch_configurations.update(forwarded)
                names = set(forwarded)
                owns_package = "imaging" in names if sensor_config is None else (
                    forwarded.get("config_file") == baseline[sensor_config])
                if owns_package:
                    child, child_context = load(package, filename, monkeypatch,
                                                **context.launch_configurations)
                    actual = selected(child.entities, child_context, Node)
                    assert [resolve(child_context, node.node_executable) for node in actual] == expected
                    assert "rviz2" not in [node.node_package for node in actual]
                    matched.append(action)
            else:
                action.execute(context)
        assert context.launch_configurations == baseline
    assert len(matched) == 1

# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Validate XT32 launch wiring without starting the driver or RViz."""

import importlib.util
from pathlib import Path

import pytest
from launch import LaunchContext
from launch.actions import DeclareLaunchArgument, GroupAction, IncludeLaunchDescription
from launch.utilities import normalize_to_list_of_substitutions, perform_substitutions
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare
from launch_ros.utilities import evaluate_parameters


PACKAGE = Path(__file__).resolve().parents[1]
ROBOT = PACKAGE.parents[1]


def resolve(context, value):
    return perform_substitutions(context, normalize_to_list_of_substitutions(value))


def load(path, monkeypatch, **overrides):
    monkeypatch.setattr(FindPackageShare, "find", lambda self, name: str(PACKAGE)
                        if name == "pandar_xt32" else str(ROBOT / "bringup" / name))
    spec = importlib.util.spec_from_file_location("xt32_test_launch", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    description = module.generate_launch_description()
    context = LaunchContext()
    context.launch_configurations.update(overrides)
    for action in description.entities:
        if isinstance(action, DeclareLaunchArgument):
            action.execute(context)
    return description, context


def sensor_launch(monkeypatch, **overrides):
    return load(PACKAGE / "launch/xt32.launch.py", monkeypatch, **overrides)


def nodes(description, context):
    return [node for node in description.entities if isinstance(node, Node)
            and (node.condition is None or node.condition.evaluate(context))]


@pytest.mark.parametrize("rviz,start_sensor,packages", [
    ("false", "true", ["hesai_ros_driver", "tf2_ros", "tf2_ros"]),
    ("true", "true", ["hesai_ros_driver", "tf2_ros", "tf2_ros", "rviz2"]),
    ("true", "false", ["rviz2"]),
    ("false", "false", []),
])
def test_sensor_and_viewer_are_independently_enabled(monkeypatch, rviz, start_sensor, packages):
    description, context = sensor_launch(monkeypatch, rviz=rviz, start_sensor=start_sensor)
    assert [node.node_package for node in nodes(description, context)] == packages


def test_default_starts_only_sensor_and_preserves_mount(monkeypatch):
    description, context = sensor_launch(monkeypatch)
    assert context.launch_configurations["rviz"] == "false"
    assert context.launch_configurations["start_sensor"] == "true"
    driver, mount, alias = nodes(description, context)
    assert resolve(context, driver.node_executable) == "hesai_ros_driver_node"
    arguments = [resolve(context, value) for value in mount._Node__arguments]
    assert arguments == ["--x", "0.34218", "--y", "0.0", "--z", "0.20",
                         "--roll", "0.0", "--pitch", "0.0", "--yaw", "0.0",
                         "--frame-id", "base_link", "--child-frame-id", "pandar_xt32"]
    assert [resolve(context, value) for value in alias._Node__arguments] == [
        "--frame-id", "pandar_xt32", "--child-frame-id", "b2/lidar_link"]


@pytest.mark.parametrize("rviz", ["false", "true"])
def test_rviz_keeps_custom_mount_and_topic(monkeypatch, rviz):
    description, context = sensor_launch(monkeypatch, rviz=rviz,
        config_file="/tmp/commissioned.yaml", points_topic="/xt32/custom_points",
        fixed_frame="custom_lidar", base_frame="vehicle", lidar_frame="custom_lidar",
        x="0.1", y="0.2", z="0.3", roll="0.4", pitch="0.5", yaw="0.6")
    selected = nodes(description, context)
    parameters = evaluate_parameters(context, selected[0]._Node__parameters)
    assert parameters[0] == {"config_path": "/tmp/commissioned.yaml"}
    arguments = [resolve(context, value) for value in selected[1]._Node__arguments]
    assert arguments == ["--x", "0.1", "--y", "0.2", "--z", "0.3",
                         "--roll", "0.4", "--pitch", "0.5", "--yaw", "0.6",
                         "--frame-id", "vehicle", "--child-frame-id", "pandar_xt32"]
    remapping = selected[0]._Node__remappings[0]
    assert tuple(resolve(context, value) for value in remapping) == (
        "/lidar_points", "/xt32/custom_points")
    if rviz == "true":
        viewer = selected[-1]
        assert [resolve(context, value) for value in viewer._Node__arguments] == [
            "-d", str(PACKAGE / "rviz/xt32.rviz"), "-f", "custom_lidar"]
        assert tuple(resolve(context, value) for value in viewer._Node__remappings[0]) == (
            "/b2/points", "/xt32/custom_points")


@pytest.mark.parametrize("lidar,expected_count", [("xt32", 1), ("none", 0)])
def test_whole_stack_does_not_start_sensor_viewer_or_leak_rviz_option(monkeypatch, lidar, expected_count):
    description, context = load(ROBOT / "bringup/robot_bringup/launch/drivers.launch.py",
                                monkeypatch, lidar=lidar, rviz="true")
    baseline = dict(context.launch_configurations)
    includes = []
    for group in description.entities:
        if not isinstance(group, GroupAction):
            continue
        for action in group.execute(context):
            if isinstance(action, IncludeLaunchDescription):
                if action.condition is not None and not action.condition.evaluate(context):
                    continue
                arguments = {name: resolve(context, value) for name, value in action.launch_arguments}
                assert arguments["rviz"] == "false"
                assert arguments["start_sensor"] == "true"
                context.launch_configurations.update(arguments)
                child, child_context = sensor_launch(monkeypatch, **context.launch_configurations)
                assert "rviz2" not in [node.node_package for node in nodes(child, child_context)]
                includes.append(action)
            else:
                action.execute(context)
    assert len(includes) == expected_count
    assert context.launch_configurations == baseline


def test_single_launch_file_and_preserved_rviz_config():
    assert {path.name for path in (PACKAGE / "launch").glob("*.launch.py")} == {"xt32.launch.py"}
    assert (PACKAGE / "rviz/xt32.rviz").is_file()

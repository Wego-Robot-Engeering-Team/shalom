# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Check launch roles, argument forwarding and scoping without executing nodes."""

from collections import Counter
import importlib.util
import json
from pathlib import Path
import xml.etree.ElementTree as ET

import pytest
import yaml
from launch import LaunchContext
from launch.actions import DeclareLaunchArgument, GroupAction, IncludeLaunchDescription, OpaqueFunction
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.utilities import normalize_to_list_of_substitutions, perform_substitutions
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare
from launch_ros.utilities import evaluate_parameters


ROOT = Path(__file__).resolve().parents[4]
SHARES = {
    "robot_bringup": ROOT / "robot/bringup/robot_bringup",
    "navigation": ROOT / "robot/l2_control/navigation",
    "hmi_bridge": ROOT / "robot/l4_communication/hmi_bridge",
    "simulation_bringup": ROOT / "simulation/simulation_bringup",
}


def resolve(context, value):
    return perform_substitutions(context, normalize_to_list_of_substitutions(value))


class SourceWithLocation(PythonLaunchDescriptionSource):
    def __init__(self, location):
        self.declared_location = location
        super().__init__(location)


def load(package, filename, monkeypatch):
    path = SHARES[package] / "launch" / filename
    spec = importlib.util.spec_from_file_location(f"{package}_{filename.replace('.', '_')}", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    monkeypatch.setattr(module, "PythonLaunchDescriptionSource", SourceWithLocation, raising=False)
    monkeypatch.setattr(module, "get_package_share_directory",
                        lambda name: str(SHARES.get(name, Path("/test/share") / name)), raising=False)
    return module


def configure(description, **overrides):
    context = LaunchContext()
    context.launch_configurations.update(overrides)
    for entity in description.entities:
        if isinstance(entity, DeclareLaunchArgument):
            entity.execute(context)
    return context


def includes(entities, context):
    result = {}
    for entity in entities:
        if entity.condition is not None and not entity.condition.evaluate(context):
            continue
        if isinstance(entity, IncludeLaunchDescription):
            path = resolve(context, entity.launch_description_source.declared_location)
            result[Path(path).name] = entity
        elif isinstance(entity, GroupAction):
            result.update(includes(entity.get_sub_entities(), context))
    return result


def forwarded(include, context):
    return {name: resolve(context, value) for name, value in include.launch_arguments}


def node_id(node, context):
    return node.node_package, resolve(context, node.node_executable)


@pytest.fixture(autouse=True)
def source_lookup(monkeypatch):
    monkeypatch.syspath_prepend(str(SHARES["robot_bringup"]))
    def share(self, context):
        package = perform_substitutions(context, self.package)
        return str(SHARES.get(package, Path("/test/share") / package))
    monkeypatch.setattr(FindPackageShare, "perform", share)


@pytest.mark.parametrize("filename,expected", [
    ("control.launch.py", {"twist_mux", "joint_mux", "safety_gate"}),
    ("system.launch.py", {"mission_manager", "safety_manager", "motion_interlock_manager"}),
    ("communication.launch.py", {"teleop_bridge"}),
])
def test_launch_roles_are_separate(monkeypatch, filename, expected):
    description = load("robot_bringup", filename, monkeypatch).generate_launch_description()
    assert {entity.node_package for entity in description.entities if isinstance(entity, Node)} == expected


@pytest.mark.parametrize("package,time,odometry,peer,rviz", [
    ("robot_bringup", "false", "/b2/odom", "", "false"),
    ("simulation_bringup", "true", "/b2/odom_gt", "127.0.0.1", "true"),
])
def test_entrypoints_share_runtime_and_preserve_platform_defaults(
    monkeypatch, package, time, odometry, peer, rviz,
):
    description = load(package, "bringup.launch.py", monkeypatch).generate_launch_description()
    context = configure(description)
    children = includes(description.entities, context)
    assert "runtime.launch.py" in children
    assert "navigation.launch.py" not in children
    assert "control.launch.py" not in children
    arguments = forwarded(children["runtime.launch.py"], context)
    assert arguments["use_sim_time"] == time
    assert arguments["base_odometry_topic"] == odometry
    assert arguments["base_output_topic"] == "/cmd_vel"
    assert arguments["teleop_allowed_peer"] == peer
    assert arguments["rviz"] == rviz
    assert arguments["map"] == "auto"
    if package == "robot_bringup":
        assert "drivers.launch.py" in children
        metadata = yaml.safe_load((SHARES[package] / "config/robot_metadata.yaml").read_text())
        assert arguments["robot_id"] == metadata["robot"]["id"]
    else:
        assert "b2_sim.launch.py" in children
        assert arguments["robot_id"] == "SIM-B2-1"
        assert arguments["bridge_config"].endswith("config/bridge_sim.yaml")


@pytest.mark.parametrize("package", ["robot_bringup", "simulation_bringup"])
def test_map_preflight_runs_before_drivers(monkeypatch, tmp_path, package):
    description = load(package, "bringup.launch.py", monkeypatch).generate_launch_description()
    context = configure(description, maps_dir=str(tmp_path))
    (tmp_path / "default_map.json").write_text('{"map_id": "missing"}')
    actions = description.entities
    preflight = next(action for action in actions if isinstance(action, OpaqueFunction))
    first_process = next(action for action in actions if isinstance(action, (IncludeLaunchDescription, Node)))
    assert actions.index(preflight) < actions.index(first_process)
    with pytest.raises(RuntimeError):
        preflight.execute(context)


@pytest.mark.parametrize("time,odometry", [("false", "/b2/odom"), ("true", "/b2/odom_gt")])
def test_runtime_has_each_endpoint_once_and_preserves_parameters(monkeypatch, time, odometry):
    module = load("robot_bringup", "runtime.launch.py", monkeypatch)
    description = module.generate_launch_description()
    context = configure(description, robot_id="TEST-42", use_sim_time=time,
                        base_odometry_topic=odometry, teleop_allowed_peer="192.0.2.10",
                        map="none", rviz="true")
    for action in module._resolve_default_map(context):
        action.execute(context)
    children = includes(description.entities, context)
    assert set(children) == {"navigation.launch.py", "system.launch.py", "control.launch.py",
                             "communication.launch.py", "rviz.launch.py"}
    collected = []
    for filename, child in children.items():
        package = "navigation" if filename == "navigation.launch.py" else "robot_bringup"
        leaf = load(package, filename, monkeypatch).generate_launch_description()
        leaf_context = configure(leaf, **forwarded(child, context))
        for node in leaf.entities:
            if not isinstance(node, Node):
                continue
            collected.append(node_id(node, leaf_context))
            params = evaluate_parameters(leaf_context, node._Node__parameters)
            assert params[-1]["use_sim_time"] is (time == "true")
            if node.node_package == "safety_gate":
                assert params[-1]["output_base_topic"] == "/cmd_vel"
                assert params[-1]["authority_timeout_ms"] == 500
            if node.node_package == "safety_manager":
                assert params[-1]["require_external_heartbeat"] is True
            if resolve(leaf_context, node.node_executable) == "base_motion_monitor_node":
                assert params[-1]["odometry_topic"] == odometry
            if node.node_package == "teleop_bridge":
                assert params[-1]["robot_id"] == "TEST-42"
                assert params[-1]["allowed_peer"] == "192.0.2.10"
                assert params[-1]["output_topic"] == "/motion/teleop/cmd_vel"
        if filename == "communication.launch.py":
            tcp = includes(leaf.entities, leaf_context)["bridge.launch.py"]
            bridge_description = load("hmi_bridge", "bridge.launch.py", monkeypatch).generate_launch_description()
            tcp_context = configure(bridge_description, **forwarded(tcp, leaf_context))
            for node in bridge_description.entities:
                if isinstance(node, Node):
                    collected.append(node_id(node, tcp_context))
                    params = evaluate_parameters(tcp_context, node._Node__parameters)[-1]
                    assert params["robot_id"] == "TEST-42"
                    assert params["use_sim_time"] is (time == "true")
    assert Counter(collected) == Counter({
        ("mission_manager", "mission_manager_node"): 1,
        ("safety_manager", "safety_manager_node"): 1,
        ("motion_interlock_manager", "motion_interlock_manager_node"): 1,
        ("motion_interlock_manager", "base_motion_monitor_node"): 1,
        ("twist_mux", "twist_mux"): 1, ("joint_mux", "joint_mux_node"): 1,
        ("safety_gate", "safety_gate_node"): 1, ("teleop_bridge", "teleop_bridge_node"): 1,
        ("hmi_bridge", "hmi_bridge_node"): 1, ("estop_bridge", "estop_bridge_node"): 1,
        ("nav2_map_server", "map_server"): 1, ("nav2_amcl", "amcl"): 1,
        ("nav2_lifecycle_manager", "lifecycle_manager"): 1, ("rviz2", "rviz2"): 1,
    })


def test_bridge_disabled_preserves_udp_and_system_safety(monkeypatch):
    description = load("robot_bringup", "communication.launch.py", monkeypatch).generate_launch_description()
    context = configure(description, robot_id="TEST", bridge="false")
    assert includes(description.entities, context) == {}
    assert [entity.node_package for entity in description.entities if isinstance(entity, Node)] == ["teleop_bridge"]


def test_runtime_scopes_do_not_leak_child_configuration(monkeypatch):
    description = load("robot_bringup", "runtime.launch.py", monkeypatch).generate_launch_description()
    context = configure(description, robot_id="TEST", map="none", rviz="true")
    for group in description.entities:
        if not isinstance(group, GroupAction):
            continue
        baseline = dict(context.launch_configurations)
        for action in group.execute(context):
            if isinstance(action, IncludeLaunchDescription):
                context.launch_configurations["params_file"] = "/child/generated.yaml"
                context.launch_configurations["use_sim_time"] = "opposite"
            else:
                action.execute(context)
        assert context.launch_configurations == baseline


@pytest.mark.parametrize("mode", ["auto", "none", "", "explicit"])
def test_shared_map_resolution(monkeypatch, tmp_path, mode):
    module = load("robot_bringup", "runtime.launch.py", monkeypatch)
    directory = tmp_path / "maps" / "inspection"
    directory.mkdir(parents=True)
    map_file = directory / "map.yaml"
    map_file.write_text("image: map.pgm\n")
    (directory.parent / "default_map.json").write_text(json.dumps({"map_id": "inspection"}))
    requested = str(map_file) if mode == "explicit" else mode
    context = LaunchContext()
    context.launch_configurations.update(map=requested, maps_dir=str(directory.parent))
    for action in module._resolve_default_map(context):
        action.execute(context)
    assert context.launch_configurations["map"] == (str(map_file) if mode in ("auto", "explicit") else "")


@pytest.mark.parametrize("map_id", ["../outside", "missing", 1, None, "nested/map"])
def test_invalid_default_map_fails_before_runtime(monkeypatch, tmp_path, map_id):
    module = load("robot_bringup", "runtime.launch.py", monkeypatch)
    (tmp_path / "default_map.json").write_text(json.dumps({"map_id": map_id}))
    context = LaunchContext()
    context.launch_configurations.update(map="auto", maps_dir=str(tmp_path))
    with pytest.raises(RuntimeError):
        module._resolve_default_map(context)


def test_missing_default_map_starts_mapless(monkeypatch, tmp_path):
    module = load("robot_bringup", "runtime.launch.py", monkeypatch)
    context = LaunchContext()
    context.launch_configurations.update(map="auto", maps_dir=str(tmp_path))
    for action in module._resolve_default_map(context):
        action.execute(context)
    assert context.launch_configurations["map"] == ""


def test_navigation_owns_bt_and_visualization():
    config = yaml.safe_load((SHARES["navigation"] / "config/nav2.yaml").read_text())
    tree = config["bt_navigator"]["ros__parameters"]["default_nav_to_pose_bt_xml"]
    assert tree.startswith("$(find-pkg-share navigation)/behavior_trees/")
    filename = tree.split("/behavior_trees/", 1)[1]
    ET.parse(SHARES["navigation"] / "behavior_trees" / filename)
    assert (SHARES["navigation"] / "licenses/LICENSE-APACHE-2.0.txt").is_file()
    for profile in ("slam", "nav2", "slam_nav2"):
        assert (SHARES["navigation"] / "rviz" / f"{profile}.rviz").is_file()
